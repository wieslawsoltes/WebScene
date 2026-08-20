    static void canvas_no_op(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto feature = info.Data().IsEmpty()
            ? std::string("unknown")
            : to_utf8(info.GetIsolate(), info.Data());
        self->record_feature(
            "canvas",
            feature,
            "unsupported",
            {},
            "native-binding");
    }

    static void canvas_text_no_op(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "fillText");
        if (state == nullptr || state->node == nullptr) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++state->node->mutable_canvas().fill_text_calls;
#endif
        if (info.Length() < 3) return;
        canvas_emit_paint_state(info, *state, canvas_paint_fill_text);
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto text = to_utf8(info.GetIsolate(), info[0]);
        canvas_append_resource_command(
            *state->node,
            canvas_command_kind::fill_text,
            canvas_intern_string(*state->node, text),
            {info[1]->NumberValue(context).FromMaybe(0),
             info[2]->NumberValue(context).FromMaybe(0),
             info.Length() > 3
                ? info[3]->NumberValue(context).FromMaybe(0)
                : 0});
        if (info.Length() > 3) {
            state->node->mutable_canvas().commands.back().flags |=
                WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH;
        }
    }

    static void canvas_stroke_text_no_op(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "strokeText");
        if (state == nullptr || state->node == nullptr) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++state->node->mutable_canvas().stroke_text_calls;
#endif
        if (info.Length() < 3) return;
        canvas_emit_paint_state(info, *state, canvas_paint_stroke_text);
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto text = to_utf8(info.GetIsolate(), info[0]);
        canvas_append_resource_command(
            *state->node,
            canvas_command_kind::stroke_text,
            canvas_intern_string(*state->node, text),
            {info[1]->NumberValue(context).FromMaybe(0),
             info[2]->NumberValue(context).FromMaybe(0),
             info.Length() > 3
                ? info[3]->NumberValue(context).FromMaybe(0)
                : 0});
        if (info.Length() > 3) {
            state->node->mutable_canvas().commands.back().flags |=
                WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH;
        }
    }

    static canvas_state* canvas_path_state(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* feature = nullptr)
    {
        if (feature != nullptr) {
            current(info.GetIsolate())->record_canvas_feature(feature);
        }
        return info.Data()->IsExternal()
            ? static_cast<canvas_state*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    enum class canvas_command_kind : uint32_t {
        save = 1,
        restore = 2,
        reset_transform = 3,
        set_transform = 4,
        transform = 5,
        translate = 6,
        scale = 7,
        rotate = 8,
        begin_path = 9,
        close_path = 10,
        move_to = 11,
        line_to = 12,
        bezier_curve_to = 13,
        quadratic_curve_to = 14,
        arc = 15,
        arc_to = 16,
        rect = 17,
        clip = 18,
        set_line_dash = 19,
        stroke = 20,
        fill = 21,
        fill_rect = 22,
        stroke_rect = 23,
        clear_rect = 24,
        fill_text = 25,
        stroke_text = 26,
        draw_canvas = 27,
        fill_svg_path = 28,
        stroke_svg_path = 29,
        fill_style = 40,
        stroke_style = 41,
        line_width = 42,
        line_cap = 43,
        line_join = 44,
        miter_limit = 45,
        global_alpha = 46,
        line_dash_offset = 47,
        font = 48,
        text_align = 49,
        text_baseline = 50,
        image_smoothing_enabled = 51,
        image_smoothing_quality = 52,
        global_composite_operation = 53,
        shadow_color = 54,
        shadow_blur = 55,
        shadow_offset_x = 56,
        shadow_offset_y = 57
    };

    enum canvas_paint_state_mask : uint32_t {
        canvas_paint_fill = 1U << 0U,
        canvas_paint_stroke = 1U << 1U,
        canvas_paint_line = 1U << 2U,
        canvas_paint_alpha = 1U << 3U,
        canvas_paint_dash = 1U << 4U,
        canvas_paint_text = 1U << 5U,
        canvas_paint_image = 1U << 6U,
        canvas_paint_composite = 1U << 7U,
        canvas_paint_shadow = 1U << 8U,
        canvas_paint_all = (1U << 9U) - 1U,
        canvas_paint_fill_rect = canvas_paint_fill
            | canvas_paint_alpha
            | canvas_paint_composite
            | canvas_paint_shadow,
        canvas_paint_stroke_path = canvas_paint_stroke
            | canvas_paint_line
            | canvas_paint_alpha
            | canvas_paint_dash
            | canvas_paint_composite
            | canvas_paint_shadow,
        canvas_paint_fill_text = canvas_paint_fill_rect
            | canvas_paint_text,
        canvas_paint_stroke_text = canvas_paint_stroke_path
            | canvas_paint_text,
        canvas_paint_draw_image = canvas_paint_alpha
            | canvas_paint_image
            | canvas_paint_composite
            | canvas_paint_shadow
    };

    static void canvas_append_command(
        dom_node& node,
        canvas_command_kind kind,
        std::initializer_list<double> arguments = {})
    {
        mark_canvas_scene_changed();
        webscene_canvas_command command{};
        command.kind = static_cast<uint32_t>(kind);
        command.flags = static_cast<uint32_t>(arguments.size());
        size_t index = 0;
        for (const auto argument : arguments) {
            if (index == 8U) break;
            command.data.values[index++] = argument;
        }
        node.mutable_canvas().commands.push_back(command);
    }

    static void canvas_append_resource_command(
        dom_node& node,
        canvas_command_kind kind,
        uint32_t resource_id,
        std::initializer_list<double> arguments = {})
    {
        mark_canvas_scene_changed();
        webscene_canvas_command command{};
        command.kind = static_cast<uint32_t>(kind);
        command.flags = static_cast<uint32_t>(arguments.size());
        command.resource_id = resource_id;
        size_t index = 0;
        for (const auto argument : arguments) {
            if (index == 8U) break;
            command.data.values[index++] = argument;
        }
        node.mutable_canvas().commands.push_back(command);
    }

    static void canvas_append_line_dash_command(
        dom_node& node,
        const std::vector<double>& segments)
    {
        mark_canvas_scene_changed();
        webscene_canvas_command command{};
        command.kind = static_cast<uint32_t>(canvas_command_kind::set_line_dash);
        command.flags = static_cast<uint32_t>(segments.size() + 1U);
        command.data.values[0] = static_cast<double>(segments.size());
        for (size_t index = 0; index < segments.size() && index < 7U; ++index) {
            command.data.values[index + 1U] = segments[index];
        }
        node.mutable_canvas().commands.push_back(command);
    }

    static uint32_t canvas_intern_string(dom_node& node, const std::string& value)
    {
        auto& canvas = node.mutable_canvas();
        if (const auto known = canvas.string_indices.find(value);
            known != canvas.string_indices.end()) {
            return known->second;
        }
        const auto index = static_cast<uint32_t>(canvas.strings.size());
        canvas.strings.push_back(value);
        canvas.string_indices.emplace(value, index);
        return index;
    }

    static double canvas_number_property(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* name,
        double fallback)
    {
        const auto context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (!info.This()->Get(
                context,
                js_string(info.GetIsolate(), name))
                .ToLocal(&value)) {
            return fallback;
        }
        const auto result = value->NumberValue(context).FromMaybe(fallback);
        return std::isfinite(result) ? result : fallback;
    }

    static bool canvas_boolean_property(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* name,
        bool fallback)
    {
        v8::Local<v8::Value> value;
        return info.This()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), name))
                .ToLocal(&value)
            ? value->BooleanValue(info.GetIsolate())
            : fallback;
    }

    static std::pair<double, double> canvas_transform_point(
        const canvas_state& state,
        double x,
        double y)
    {
        return {
            state.transform.a * x + state.transform.c * y + state.transform.e,
            state.transform.b * x + state.transform.d * y + state.transform.f};
    }

    static void canvas_multiply_transform(canvas_state& state, const canvas_transform& value)
    {
        const auto current = state.transform;
        state.transform = {
            current.a * value.a + current.c * value.b,
            current.b * value.a + current.d * value.b,
            current.a * value.c + current.c * value.d,
            current.b * value.c + current.d * value.d,
            current.a * value.e + current.c * value.f + current.e,
            current.b * value.e + current.d * value.f + current.f};
    }

    static std::string canvas_string_property(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* name,
        const char* fallback)
    {
        v8::Local<v8::Value> value;
        return info.This()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), name))
                .ToLocal(&value)
            && value->IsString()
            ? to_utf8(info.GetIsolate(), value)
            : std::string(fallback);
    }

    static bool canvas_v8_string_equals(
        v8::Isolate* isolate,
        v8::Local<v8::String> value,
        std::string_view expected)
    {
        constexpr size_t stack_capacity = 128U;
        if (expected.size() > stack_capacity) return false;
        const auto utf8_length = value->Utf8LengthV2(isolate);
        if (utf8_length != expected.size()) {
            return false;
        }
        std::array<char, stack_capacity> buffer{};
        const auto written = value->WriteUtf8V2(
            isolate,
            buffer.data(),
            utf8_length,
            v8::String::WriteFlags::kReplaceInvalidUtf8);
        return written == utf8_length
            && std::equal(
                expected.begin(),
                expected.end(),
                buffer.begin());
    }

    static void canvas_emit_string_property(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        canvas_state& state,
        canvas_command_kind opcode,
        const char* name,
        const char* fallback)
    {
        const auto value = canvas_string_property(info, name, fallback);
        canvas_append_resource_command(
            *state.node,
            opcode,
            canvas_intern_string(*state.node, value));
    }

    static void canvas_emit_number_property(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        canvas_state& state,
        canvas_command_kind opcode,
        const char* name,
        double fallback)
    {
        canvas_append_command(
            *state.node,
            opcode,
            {canvas_number_property(info, name, fallback)});
    }

    static void canvas_emit_paint_state(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        canvas_state& state,
        uint32_t mask = canvas_paint_all)
    {
        if (state.node == nullptr) return;
        const auto first = !state.has_emitted_paint_state;
        auto& emitted = state.emitted_paint_state;
        const auto emit_string = [&](
            canvas_command_kind kind,
            const char* name,
            const char* fallback,
            std::string& previous) {
            v8::Local<v8::Value> property;
            const auto has_string = info.This()->Get(
                    info.GetIsolate()->GetCurrentContext(),
                    js_string(info.GetIsolate(), name))
                    .ToLocal(&property)
                && property->IsString();
            if (!first) {
                if (has_string
                    && canvas_v8_string_equals(
                        info.GetIsolate(),
                        property.As<v8::String>(),
                        previous)) {
                    return;
                }
                if (!has_string && previous == fallback) return;
            }
            auto value = has_string
                ? to_utf8(info.GetIsolate(), property)
                : std::string(fallback);
            if (first || value != previous) {
                canvas_append_resource_command(
                    *state.node,
                    kind,
                    canvas_intern_string(*state.node, value));
                previous = std::move(value);
            }
        };
        const auto emit_number = [&](
            canvas_command_kind kind,
            const char* name,
            double fallback,
            double& previous) {
            const auto value = canvas_number_property(info, name, fallback);
            if (first || value != previous) {
                canvas_append_command(*state.node, kind, {value});
                previous = value;
            }
        };

        if ((mask & canvas_paint_fill) != 0U) {
            emit_string(
                canvas_command_kind::fill_style,
                "fillStyle",
                "#000000",
                emitted.fill_style);
        }
        if ((mask & canvas_paint_stroke) != 0U) {
            emit_string(
                canvas_command_kind::stroke_style,
                "strokeStyle",
                "#000000",
                emitted.stroke_style);
        }
        if ((mask & canvas_paint_line) != 0U) {
            emit_number(canvas_command_kind::line_width, "lineWidth", 1, emitted.line_width);
            emit_string(canvas_command_kind::line_cap, "lineCap", "butt", emitted.line_cap);
            emit_string(canvas_command_kind::line_join, "lineJoin", "miter", emitted.line_join);
            emit_number(
                canvas_command_kind::miter_limit,
                "miterLimit",
                10,
                emitted.miter_limit);
        }
        if ((mask & canvas_paint_alpha) != 0U) {
            emit_number(
                canvas_command_kind::global_alpha,
                "globalAlpha",
                1,
                emitted.global_alpha);
        }
        if ((mask & canvas_paint_dash) != 0U) {
            emit_number(
                canvas_command_kind::line_dash_offset,
                "lineDashOffset",
                0,
                emitted.line_dash_offset);
            if ((first && !state.line_dash.empty())
                || state.line_dash != emitted.line_dash) {
                canvas_append_line_dash_command(*state.node, state.line_dash);
                emitted.line_dash = state.line_dash;
            }
        }
        if ((mask & canvas_paint_text) != 0U) {
            emit_string(canvas_command_kind::font, "font", "10px sans-serif", emitted.font);
            emit_string(
                canvas_command_kind::text_align,
                "textAlign",
                "start",
                emitted.text_align);
            emit_string(
                canvas_command_kind::text_baseline,
                "textBaseline",
                "alphabetic",
                emitted.text_baseline);
        }
        if ((mask & canvas_paint_image) != 0U) {
            const auto smoothing = canvas_boolean_property(
                info,
                "imageSmoothingEnabled",
                true);
            if (first || smoothing != emitted.image_smoothing_enabled) {
                canvas_append_command(
                    *state.node,
                    canvas_command_kind::image_smoothing_enabled,
                    {smoothing ? 1.0 : 0.0});
                emitted.image_smoothing_enabled = smoothing;
            }
            emit_string(
                canvas_command_kind::image_smoothing_quality,
                "imageSmoothingQuality",
                "low",
                emitted.image_smoothing_quality);
        }
        if ((mask & canvas_paint_composite) != 0U) {
            emit_string(
                canvas_command_kind::global_composite_operation,
                "globalCompositeOperation",
                "source-over",
                emitted.global_composite_operation);
        }
        if ((mask & canvas_paint_shadow) != 0U) {
            emit_string(
                canvas_command_kind::shadow_color,
                "shadowColor",
                "rgba(0, 0, 0, 0)",
                emitted.shadow_color);
            emit_number(
                canvas_command_kind::shadow_blur,
                "shadowBlur",
                0,
                emitted.shadow_blur);
            emit_number(
                canvas_command_kind::shadow_offset_x,
                "shadowOffsetX",
                0,
                emitted.shadow_offset_x);
            emit_number(
                canvas_command_kind::shadow_offset_y,
                "shadowOffsetY",
                0,
                emitted.shadow_offset_y);
        }
        state.has_emitted_paint_state = true;
    }

    static uint32_t canvas_snapshot_color(
        const std::string& value,
        double global_alpha,
        uint32_t fallback)
    {
        const auto parsed = native_document::parse_color(value);
        auto color = parsed == 0 ? fallback : parsed;
        const auto alpha = static_cast<uint32_t>(std::lround(
            static_cast<double>(color & 0xFFU)
                * std::clamp(global_alpha, 0.0, 1.0)));
        return (color & 0xFFFFFF00U) | std::min(255U, alpha);
    }

    static void canvas_emit_snapshot_state(dom_node& node, const canvas_snapshot& snapshot)
    {
        const auto emit_string = [&](canvas_command_kind kind, const std::string& value) {
            canvas_append_resource_command(
                node,
                kind,
                canvas_intern_string(node, value));
        };
        const auto emit_number = [&](canvas_command_kind kind, double value) {
            canvas_append_command(node, kind, {value});
        };

        canvas_append_command(
            node,
            canvas_command_kind::set_transform,
            {snapshot.transform.a, snapshot.transform.b, snapshot.transform.c,
             snapshot.transform.d, snapshot.transform.e, snapshot.transform.f});
        emit_string(canvas_command_kind::fill_style, snapshot.fill_style);
        emit_string(canvas_command_kind::stroke_style, snapshot.stroke_style);
        emit_number(canvas_command_kind::line_width, snapshot.line_width);
        emit_string(canvas_command_kind::line_cap, snapshot.line_cap);
        emit_string(canvas_command_kind::line_join, snapshot.line_join);
        emit_number(canvas_command_kind::miter_limit, snapshot.miter_limit);
        emit_number(canvas_command_kind::global_alpha, snapshot.global_alpha);
        emit_number(canvas_command_kind::line_dash_offset, snapshot.line_dash_offset);
        canvas_append_line_dash_command(node, snapshot.line_dash);
        emit_string(canvas_command_kind::font, snapshot.font);
        emit_string(canvas_command_kind::text_align, snapshot.text_align);
        emit_string(canvas_command_kind::text_baseline, snapshot.text_baseline);
        emit_number(
            canvas_command_kind::image_smoothing_enabled,
            snapshot.image_smoothing_enabled ? 1.0 : 0.0);
        emit_string(
            canvas_command_kind::image_smoothing_quality,
            snapshot.image_smoothing_quality);
        emit_string(
            canvas_command_kind::global_composite_operation,
            snapshot.global_composite_operation);
        emit_string(canvas_command_kind::shadow_color, snapshot.shadow_color);
        emit_number(canvas_command_kind::shadow_blur, snapshot.shadow_blur);
        emit_number(canvas_command_kind::shadow_offset_x, snapshot.shadow_offset_x);
        emit_number(canvas_command_kind::shadow_offset_y, snapshot.shadow_offset_y);
    }

    static void canvas_compact_full_overwrite(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        canvas_state& state)
    {
        auto& node = *state.node;
        auto& canvas = node.mutable_canvas();
        canvas.rects.clear();
        canvas.lines.clear();
        advance_canvas_generation(node);
        canvas.commands.clear();
        canvas.strings.clear();
        canvas.string_indices.clear();
        state.has_emitted_paint_state = false;

        // Preserve the command-side save stack after discarding dead pixels.
        // Clipped stacks are excluded by callers because clip paths are not yet
        // reconstructible from canvas_snapshot.
        for (const auto& snapshot : state.stack) {
            canvas_emit_snapshot_state(node, snapshot);
            canvas_append_command(node, canvas_command_kind::save);
        }
        canvas_emit_paint_state(info, state);
        canvas_append_command(
            node,
            canvas_command_kind::set_transform,
            {state.transform.a, state.transform.b, state.transform.c,
             state.transform.d, state.transform.e, state.transform.f});
    }

    static void canvas_save(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_state));
        auto* state = canvas_path_state(info, "save");
        if (state == nullptr) return;
        canvas_emit_paint_state(info, *state);
        canvas_append_command(*state->node, canvas_command_kind::save);
        canvas_snapshot snapshot;
        snapshot.transform = state->transform;
        snapshot.fill_style = canvas_string_property(info, "fillStyle", "#000000");
        snapshot.stroke_style = canvas_string_property(info, "strokeStyle", "#000000");
        snapshot.global_composite_operation = canvas_string_property(info, "globalCompositeOperation", "source-over");
        snapshot.line_cap = canvas_string_property(info, "lineCap", "butt");
        snapshot.line_join = canvas_string_property(info, "lineJoin", "miter");
        snapshot.font = canvas_string_property(info, "font", "10px sans-serif");
        snapshot.text_align = canvas_string_property(info, "textAlign", "start");
        snapshot.text_baseline = canvas_string_property(info, "textBaseline", "alphabetic");
        snapshot.image_smoothing_quality = canvas_string_property(info, "imageSmoothingQuality", "low");
        snapshot.shadow_color = canvas_string_property(info, "shadowColor", "rgba(0, 0, 0, 0)");
        snapshot.line_width = canvas_number_property(info, "lineWidth", 1);
        snapshot.global_alpha = canvas_number_property(info, "globalAlpha", 1);
        snapshot.miter_limit = canvas_number_property(info, "miterLimit", 10);
        snapshot.line_dash_offset = canvas_number_property(info, "lineDashOffset", 0);
        snapshot.shadow_blur = canvas_number_property(info, "shadowBlur", 0);
        snapshot.shadow_offset_x = canvas_number_property(info, "shadowOffsetX", 0);
        snapshot.shadow_offset_y = canvas_number_property(info, "shadowOffsetY", 0);
        snapshot.image_smoothing_enabled = canvas_boolean_property(info, "imageSmoothingEnabled", true);
        snapshot.has_clip = state->has_clip;
        snapshot.line_dash = state->line_dash;
        state->stack.push_back(std::move(snapshot));
    }

    static void canvas_restore(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_state));
        auto* state = canvas_path_state(info, "restore");
        if (state == nullptr || state->stack.empty()) return;
        auto snapshot = std::move(state->stack.back());
        state->stack.pop_back();
        canvas_append_command(*state->node, canvas_command_kind::restore);
        state->transform = snapshot.transform;
        state->line_dash = snapshot.line_dash;
        state->has_clip = snapshot.has_clip;
        if (state->has_emitted_paint_state) {
            state->emitted_paint_state.line_dash = state->line_dash;
        }
        const auto context = info.GetIsolate()->GetCurrentContext();
        info.This()->Set(context, js_string(info.GetIsolate(), "fillStyle"), js_string(info.GetIsolate(), snapshot.fill_style.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "strokeStyle"), js_string(info.GetIsolate(), snapshot.stroke_style.c_str())).Check();
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "globalCompositeOperation"),
            js_string(info.GetIsolate(), snapshot.global_composite_operation.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "lineWidth"), v8::Number::New(info.GetIsolate(), snapshot.line_width)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "globalAlpha"), v8::Number::New(info.GetIsolate(), snapshot.global_alpha)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "lineCap"), js_string(info.GetIsolate(), snapshot.line_cap.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "lineJoin"), js_string(info.GetIsolate(), snapshot.line_join.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "miterLimit"), v8::Number::New(info.GetIsolate(), snapshot.miter_limit)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "lineDashOffset"), v8::Number::New(info.GetIsolate(), snapshot.line_dash_offset)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "font"), js_string(info.GetIsolate(), snapshot.font.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "textAlign"), js_string(info.GetIsolate(), snapshot.text_align.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "textBaseline"), js_string(info.GetIsolate(), snapshot.text_baseline.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "imageSmoothingEnabled"), v8::Boolean::New(info.GetIsolate(), snapshot.image_smoothing_enabled)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "imageSmoothingQuality"), js_string(info.GetIsolate(), snapshot.image_smoothing_quality.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "shadowColor"), js_string(info.GetIsolate(), snapshot.shadow_color.c_str())).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "shadowBlur"), v8::Number::New(info.GetIsolate(), snapshot.shadow_blur)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "shadowOffsetX"), v8::Number::New(info.GetIsolate(), snapshot.shadow_offset_x)).Check();
        info.This()->Set(context, js_string(info.GetIsolate(), "shadowOffsetY"), v8::Number::New(info.GetIsolate(), snapshot.shadow_offset_y)).Check();
        state->emitted_paint_state = snapshot;
        state->has_emitted_paint_state = true;
    }

    static void canvas_scale(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "scale");
        if (state == nullptr || info.Length() < 2) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(1);
        const auto y = info[1]->NumberValue(context).FromMaybe(1);
        canvas_append_command(*state->node, canvas_command_kind::scale, {x, y});
        canvas_multiply_transform(*state, canvas_transform{
            x, 0, 0, y, 0, 0});
    }

    static void canvas_translate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "translate");
        if (state == nullptr || info.Length() < 2) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(0);
        const auto y = info[1]->NumberValue(context).FromMaybe(0);
        canvas_append_command(*state->node, canvas_command_kind::translate, {x, y});
        canvas_multiply_transform(*state, canvas_transform{
            1, 0, 0, 1, x, y});
    }

    static void canvas_rotate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "rotate");
        if (state == nullptr || info.Length() < 1) return;
        const auto angle = info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        canvas_append_command(*state->node, canvas_command_kind::rotate, {angle});
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);
        canvas_multiply_transform(*state, canvas_transform{cosine, sine, -sine, cosine, 0, 0});
    }

    static canvas_transform canvas_transform_arguments(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        const auto context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() >= 6) {
            return {
                info[0]->NumberValue(context).FromMaybe(1),
                info[1]->NumberValue(context).FromMaybe(0),
                info[2]->NumberValue(context).FromMaybe(0),
                info[3]->NumberValue(context).FromMaybe(1),
                info[4]->NumberValue(context).FromMaybe(0),
                info[5]->NumberValue(context).FromMaybe(0)};
        }
        return {};
    }

    static void canvas_transform_method(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "transform");
        if (state == nullptr) return;
        const auto value = canvas_transform_arguments(info);
        canvas_append_command(*state->node, canvas_command_kind::transform, {
            value.a, value.b, value.c, value.d, value.e, value.f});
        canvas_multiply_transform(*state, value);
    }

    static void canvas_set_transform(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "setTransform");
        if (state == nullptr) return;
        state->transform = canvas_transform_arguments(info);
        canvas_append_command(*state->node, canvas_command_kind::set_transform, {
            state->transform.a, state->transform.b, state->transform.c,
            state->transform.d, state->transform.e, state->transform.f});
    }

    static void canvas_reset_transform(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_transform));
        auto* state = canvas_path_state(info, "resetTransform");
        if (state == nullptr) return;
        state->transform = {};
        canvas_append_command(*state->node, canvas_command_kind::reset_transform);
    }

    static void canvas_begin_path(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "beginPath");
        if (state == nullptr) return;
        canvas_append_command(*state->node, canvas_command_kind::begin_path);
        state->path.clear();
        state->has_current = false;
    }

    static void canvas_move_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "moveTo");
        if (state == nullptr || info.Length() < 2) return;
        auto context = info.GetIsolate()->GetCurrentContext();
        const auto authored_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto authored_y = info[1]->NumberValue(context).FromMaybe(0);
        canvas_append_command(*state->node, canvas_command_kind::move_to, {authored_x, authored_y});
        const auto [x, y] = canvas_transform_point(
            *state,
            authored_x,
            authored_y);
        state->current_x = x;
        state->current_y = y;
        state->start_x = state->current_x;
        state->start_y = state->current_y;
        state->has_current = true;
    }

    static void canvas_line_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "lineTo");
        if (state == nullptr || info.Length() < 2) return;
        auto context = info.GetIsolate()->GetCurrentContext();
        const auto authored_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto authored_y = info[1]->NumberValue(context).FromMaybe(0);
        canvas_append_command(*state->node, canvas_command_kind::line_to, {authored_x, authored_y});
        const auto [x, y] = canvas_transform_point(
            *state,
            authored_x,
            authored_y);
        if (!state->has_current) {
            state->current_x = state->start_x = x;
            state->current_y = state->start_y = y;
            state->has_current = true;
            return;
        }
        state->path.push_back({state->current_x, state->current_y, x, y});
        state->current_x = x;
        state->current_y = y;
    }

    static void canvas_close_path(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "closePath");
        if (state == nullptr || !state->has_current) return;
        canvas_append_command(*state->node, canvas_command_kind::close_path);
        state->path.push_back({state->current_x, state->current_y, state->start_x, state->start_y});
        state->current_x = state->start_x;
        state->current_y = state->start_y;
    }

    static void canvas_path_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "rect");
        if (state == nullptr || info.Length() < 4) return;
        auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(0);
        const auto y = info[1]->NumberValue(context).FromMaybe(0);
        const auto width = info[2]->NumberValue(context).FromMaybe(0);
        const auto height = info[3]->NumberValue(context).FromMaybe(0);
        canvas_append_command(*state->node, canvas_command_kind::rect, {x, y, width, height});
        const auto top_left = canvas_transform_point(*state, x, y);
        const auto top_right = canvas_transform_point(*state, x + width, y);
        const auto bottom_right = canvas_transform_point(*state, x + width, y + height);
        const auto bottom_left = canvas_transform_point(*state, x, y + height);
        state->path.push_back({top_left.first, top_left.second, top_right.first, top_right.second});
        state->path.push_back({top_right.first, top_right.second, bottom_right.first, bottom_right.second});
        state->path.push_back({bottom_right.first, bottom_right.second, bottom_left.first, bottom_left.second});
        state->path.push_back({bottom_left.first, bottom_left.second, top_left.first, top_left.second});
        state->current_x = state->start_x = top_left.first;
        state->current_y = state->start_y = top_left.second;
        state->has_current = true;
    }

    static void append_canvas_curve_point(canvas_state& state, double x, double y)
    {
        const auto [transformed_x, transformed_y] = canvas_transform_point(state, x, y);
        if (!state.has_current) {
            state.current_x = state.start_x = transformed_x;
            state.current_y = state.start_y = transformed_y;
            state.has_current = true;
            return;
        }
        state.path.push_back({state.current_x, state.current_y, transformed_x, transformed_y});
        state.current_x = transformed_x;
        state.current_y = transformed_y;
    }

    static void canvas_quadratic_curve_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "quadraticCurveTo");
        if (state == nullptr || info.Length() < 4 || !state->has_current) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto control_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto control_y = info[1]->NumberValue(context).FromMaybe(0);
        const auto end_x = info[2]->NumberValue(context).FromMaybe(0);
        const auto end_y = info[3]->NumberValue(context).FromMaybe(0);
        canvas_append_command(
            *state->node,
            canvas_command_kind::quadratic_curve_to,
            {control_x, control_y, end_x, end_y});
        const auto start_x = state->current_x;
        const auto start_y = state->current_y;
        const auto transformed_control = canvas_transform_point(*state, control_x, control_y);
        const auto transformed_end = canvas_transform_point(*state, end_x, end_y);
        for (int index = 1; index <= 16; ++index) {
            const auto t = static_cast<double>(index) / 16.0;
            const auto one_minus = 1.0 - t;
            const auto x = one_minus * one_minus * start_x
                + 2.0 * one_minus * t * transformed_control.first
                + t * t * transformed_end.first;
            const auto y = one_minus * one_minus * start_y
                + 2.0 * one_minus * t * transformed_control.second
                + t * t * transformed_end.second;
            state->path.push_back({state->current_x, state->current_y, x, y});
            state->current_x = x;
            state->current_y = y;
        }
    }

    static void canvas_bezier_curve_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "bezierCurveTo");
        if (state == nullptr || info.Length() < 6 || !state->has_current) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto authored_control1_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto authored_control1_y = info[1]->NumberValue(context).FromMaybe(0);
        const auto authored_control2_x = info[2]->NumberValue(context).FromMaybe(0);
        const auto authored_control2_y = info[3]->NumberValue(context).FromMaybe(0);
        const auto authored_end_x = info[4]->NumberValue(context).FromMaybe(0);
        const auto authored_end_y = info[5]->NumberValue(context).FromMaybe(0);
        canvas_append_command(
            *state->node,
            canvas_command_kind::bezier_curve_to,
            {authored_control1_x, authored_control1_y,
             authored_control2_x, authored_control2_y,
             authored_end_x, authored_end_y});
        const auto control1 = canvas_transform_point(
            *state,
            authored_control1_x,
            authored_control1_y);
        const auto control2 = canvas_transform_point(
            *state,
            authored_control2_x,
            authored_control2_y);
        const auto end = canvas_transform_point(
            *state,
            authored_end_x,
            authored_end_y);
        const auto start_x = state->current_x;
        const auto start_y = state->current_y;
        for (int index = 1; index <= 20; ++index) {
            const auto t = static_cast<double>(index) / 20.0;
            const auto one_minus = 1.0 - t;
            const auto x = one_minus * one_minus * one_minus * start_x
                + 3.0 * one_minus * one_minus * t * control1.first
                + 3.0 * one_minus * t * t * control2.first
                + t * t * t * end.first;
            const auto y = one_minus * one_minus * one_minus * start_y
                + 3.0 * one_minus * one_minus * t * control1.second
                + 3.0 * one_minus * t * t * control2.second
                + t * t * t * end.second;
            state->path.push_back({state->current_x, state->current_y, x, y});
            state->current_x = x;
            state->current_y = y;
        }
    }

    static void canvas_arc(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "arc");
        if (state == nullptr || info.Length() < 5) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto center_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto center_y = info[1]->NumberValue(context).FromMaybe(0);
        const auto radius = std::abs(info[2]->NumberValue(context).FromMaybe(0));
        auto start = info[3]->NumberValue(context).FromMaybe(0);
        auto end = info[4]->NumberValue(context).FromMaybe(0);
        const auto anticlockwise = info.Length() > 5 && info[5]->BooleanValue(info.GetIsolate());
        canvas_append_command(
            *state->node,
            canvas_command_kind::arc,
            {center_x, center_y, radius, start, end, anticlockwise ? 1.0 : 0.0});
        constexpr double full_circle = 6.28318530717958647692;
        if (!anticlockwise && end < start) end += full_circle;
        if (anticlockwise && end > start) end -= full_circle;
        const auto steps = std::max(4, static_cast<int>(std::ceil(std::abs(end - start) / full_circle * 32.0)));
        for (int index = 0; index <= steps; ++index) {
            const auto angle = start + (end - start) * static_cast<double>(index) / steps;
            const auto point = canvas_transform_point(
                *state,
                center_x + std::cos(angle) * radius,
                center_y + std::sin(angle) * radius);
            if (!state->has_current) {
                state->current_x = state->start_x = point.first;
                state->current_y = state->start_y = point.second;
                state->has_current = true;
            } else {
                state->path.push_back({state->current_x, state->current_y, point.first, point.second});
                state->current_x = point.first;
                state->current_y = point.second;
            }
        }
    }

    static void canvas_arc_to(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "arcTo");
        if (state == nullptr || info.Length() < 5) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto authored_x1 = info[0]->NumberValue(context).FromMaybe(0);
        const auto authored_y1 = info[1]->NumberValue(context).FromMaybe(0);
        const auto authored_x2 = info[2]->NumberValue(context).FromMaybe(0);
        const auto authored_y2 = info[3]->NumberValue(context).FromMaybe(0);
        const auto authored_radius = info[4]->NumberValue(context).FromMaybe(0);
        if (authored_radius < 0) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(), "arcTo radius must be non-negative")));
            return;
        }
        canvas_append_command(
            *state->node,
            canvas_command_kind::arc_to,
            {authored_x1, authored_y1, authored_x2, authored_y2, authored_radius});

        const auto point1 = canvas_transform_point(*state, authored_x1, authored_y1);
        const auto point2 = canvas_transform_point(*state, authored_x2, authored_y2);
        if (!state->has_current) {
            state->current_x = state->start_x = point1.first;
            state->current_y = state->start_y = point1.second;
            state->has_current = true;
            return;
        }

        const auto scale_x = std::hypot(state->transform.a, state->transform.b);
        const auto scale_y = std::hypot(state->transform.c, state->transform.d);
        const auto radius = authored_radius * (scale_x + scale_y) * 0.5;
        const auto v1_x = state->current_x - point1.first;
        const auto v1_y = state->current_y - point1.second;
        const auto v2_x = point2.first - point1.first;
        const auto v2_y = point2.second - point1.second;
        const auto length1 = std::hypot(v1_x, v1_y);
        const auto length2 = std::hypot(v2_x, v2_y);
        constexpr double epsilon = 1e-9;
        const auto cross = v1_x * v2_y - v1_y * v2_x;
        if (radius <= epsilon || length1 <= epsilon || length2 <= epsilon
            || std::abs(cross) <= epsilon) {
            state->path.push_back({
                state->current_x, state->current_y, point1.first, point1.second});
            state->current_x = point1.first;
            state->current_y = point1.second;
            return;
        }

        const auto unit1_x = v1_x / length1;
        const auto unit1_y = v1_y / length1;
        const auto unit2_x = v2_x / length2;
        const auto unit2_y = v2_y / length2;
        const auto dot = std::clamp(unit1_x * unit2_x + unit1_y * unit2_y, -1.0, 1.0);
        const auto angle = std::acos(dot);
        const auto tangent = std::tan(angle * 0.5);
        if (std::abs(tangent) <= epsilon) {
            state->path.push_back({
                state->current_x, state->current_y, point1.first, point1.second});
            state->current_x = point1.first;
            state->current_y = point1.second;
            return;
        }
        const auto tangent_distance = radius / tangent;
        const auto tangent1_x = point1.first + unit1_x * tangent_distance;
        const auto tangent1_y = point1.second + unit1_y * tangent_distance;
        const auto tangent2_x = point1.first + unit2_x * tangent_distance;
        const auto tangent2_y = point1.second + unit2_y * tangent_distance;
        const auto bisector_x = unit1_x + unit2_x;
        const auto bisector_y = unit1_y + unit2_y;
        const auto bisector_length = std::hypot(bisector_x, bisector_y);
        if (bisector_length <= epsilon) {
            state->path.push_back({
                state->current_x, state->current_y, point1.first, point1.second});
            state->current_x = point1.first;
            state->current_y = point1.second;
            return;
        }
        const auto center_distance = radius / std::sin(angle * 0.5);
        const auto center_x = point1.first + bisector_x / bisector_length * center_distance;
        const auto center_y = point1.second + bisector_y / bisector_length * center_distance;
        state->path.push_back({
            state->current_x, state->current_y, tangent1_x, tangent1_y});
        state->current_x = tangent1_x;
        state->current_y = tangent1_y;
        auto start_angle = std::atan2(tangent1_y - center_y, tangent1_x - center_x);
        auto end_angle = std::atan2(tangent2_y - center_y, tangent2_x - center_x);
        constexpr double full_circle = 6.28318530717958647692;
        if (cross < 0) {
            while (end_angle < start_angle) end_angle += full_circle;
        } else {
            while (end_angle > start_angle) end_angle -= full_circle;
        }
        for (int index = 1; index <= 12; ++index) {
            const auto angle_at = start_angle
                + (end_angle - start_angle) * static_cast<double>(index) / 12.0;
            const auto x = center_x + std::cos(angle_at) * radius;
            const auto y = center_y + std::sin(angle_at) * radius;
            state->path.push_back({state->current_x, state->current_y, x, y});
            state->current_x = x;
            state->current_y = y;
        }
    }

    static bool canvas_emit_path_2d(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        canvas_state& state,
        canvas_command_kind kind,
        bool even_odd)
    {
        if (info.Length() == 0 || !info[0]->IsObject()) return false;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto path = info[0].As<v8::Object>();
        v8::Local<v8::Value> entries_value;
        if (path->Get(
                context,
                js_string(info.GetIsolate(), "__websceneSvgPathEntries")).ToLocal(&entries_value)
            && entries_value->IsArray()) {
            const auto entries = entries_value.As<v8::Array>();
            for (uint32_t index = 0; index < entries->Length(); ++index) {
                v8::Local<v8::Value> entry_value;
                if (!entries->Get(context, index).ToLocal(&entry_value)
                    || !entry_value->IsObject()) continue;
                const auto entry = entry_value.As<v8::Object>();
                v8::Local<v8::Value> data_value;
                if (!entry->Get(
                        context,
                        js_string(info.GetIsolate(), "data")).ToLocal(&data_value)
                    || !data_value->IsString()) continue;
                const auto number = [&](const char* name, double fallback) {
                    v8::Local<v8::Value> value;
                    if (!entry->Get(
                            context,
                            js_string(info.GetIsolate(), name)).ToLocal(&value)
                        || !value->IsNumber()) return fallback;
                    const auto result = value->NumberValue(context).FromMaybe(fallback);
                    return std::isfinite(result) ? result : fallback;
                };
                canvas_emit_paint_state(
                    info,
                    state,
                    kind == canvas_command_kind::fill_svg_path
                        ? canvas_paint_fill_rect
                        : canvas_paint_stroke_path);
                canvas_append_resource_command(
                    *state.node,
                    kind,
                    canvas_intern_string(
                        *state.node,
                        to_utf8(info.GetIsolate(), data_value)),
                    {number("a", 1), number("b", 0), number("c", 0),
                     number("d", 1), number("e", 0), number("f", 0)});
                if (even_odd) {
                    state.node->mutable_canvas().commands.back().flags |=
                        WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD;
                }
            }
            // Empty Path2D arguments also must not paint the current context path.
            return true;
        }
        v8::Local<v8::Value> path_data;
        if (path->Get(
                context,
                js_string(info.GetIsolate(), "__websceneSvgPathData")).ToLocal(&path_data)
            && path_data->IsString()) {
            canvas_emit_paint_state(
                info,
                state,
                kind == canvas_command_kind::fill_svg_path
                    ? canvas_paint_fill_rect
                    : canvas_paint_stroke_path);
            canvas_append_resource_command(
                *state.node,
                kind,
                canvas_intern_string(*state.node, to_utf8(info.GetIsolate(), path_data)),
                {1, 0, 0, 1, 0, 0});
            if (even_odd) {
                state.node->mutable_canvas().commands.back().flags |=
                    WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD;
            }
        }
        // An object selects the Path2D overload, never the current-path overload.
        return true;
    }

    static void canvas_stroke(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "stroke");
        if (state == nullptr || state->node == nullptr) return;
        if (canvas_emit_path_2d(
                info,
                *state,
                canvas_command_kind::stroke_svg_path,
                false)) return;
        if (state->path.empty()) return;
        canvas_emit_paint_state(info, *state, canvas_paint_stroke_path);
        canvas_append_command(*state->node, canvas_command_kind::stroke);
        const auto line_width = state->emitted_paint_state.line_width;
        const auto color = canvas_snapshot_color(
            state->emitted_paint_state.stroke_style,
            state->emitted_paint_state.global_alpha,
            0x000000FFU);
        constexpr size_t maximum_canvas_lines = 200000;
        auto& lines = state->node->mutable_canvas().lines;
        if (lines.size() + state->path.size() > maximum_canvas_lines) lines.clear();
        for (const auto& segment : state->path) {
            lines.push_back(canvas_line_command{
                static_cast<float>(segment.x1),
                static_cast<float>(segment.y1),
                static_cast<float>(segment.x2),
                static_cast<float>(segment.y2),
                static_cast<float>(line_width),
                color});
        }
    }

    static uint32_t canvas_color(const v8::FunctionCallbackInfo<v8::Value>& info, const char* property, uint32_t fallback)
    {
        v8::Local<v8::Value> value;
        if (!info.This()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), property)).ToLocal(&value)
            || !value->IsString()) {
            return fallback;
        }
        const auto parsed = native_document::parse_color(to_utf8(info.GetIsolate(), value));
        auto color = parsed == 0 ? fallback : parsed;
        v8::Local<v8::Value> global_alpha_value;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto global_alpha = info.This()->Get(
                context,
                js_string(info.GetIsolate(), "globalAlpha")).ToLocal(&global_alpha_value)
            ? std::clamp(global_alpha_value->NumberValue(context).FromMaybe(1), 0.0, 1.0)
            : 1.0;
        const auto alpha = static_cast<uint32_t>(std::lround(
            static_cast<double>(color & 0xFFU) * global_alpha));
        return (color & 0xFFFFFF00U) | std::min(255U, alpha);
    }

    static void append_canvas_rect(dom_node& node, double x, double y, double width, double height, uint32_t rgba)
    {
        constexpr size_t maximum_canvas_rects = 100000;
        auto& rects = node.mutable_canvas().rects;
        if (rects.size() >= maximum_canvas_rects) {
            rects.erase(
                rects.begin(),
                rects.begin() + static_cast<std::ptrdiff_t>(maximum_canvas_rects / 2));
        }
        rects.push_back(canvas_rect_command{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(width),
            static_cast<float>(height),
            rgba});
    }

    static uint32_t canvas_apply_global_alpha(uint32_t color, double global_alpha)
    {
        const auto alpha = static_cast<uint32_t>(std::lround(
            static_cast<double>(color & 0xFFU)
                * std::clamp(global_alpha, 0.0, 1.0)));
        return (color & 0xFFFFFF00U) | std::min(255U, alpha);
    }

    static void canvas_draw_image(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "drawImage");
        if (state == nullptr || state->node == nullptr) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++state->node->mutable_canvas().draw_image_calls;
#endif
        if (info.Length() < 3 || !info[0]->IsObject()) {
            return;
        }
        auto* source = unwrap_node(info[0].As<v8::Object>());
        if (source == nullptr || source->tag != "canvas") return;
        if (source == state->node) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++state->node->mutable_canvas().self_draw_image_calls;
#endif
            return;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++state->node->mutable_canvas().canvas_draw_image_calls;
#endif

        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto source_width = element_dimension(*source, "width", 300);
        const auto source_height = element_dimension(*source, "height", 150);
        double source_x = 0;
        double source_y = 0;
        double source_draw_width = source_width;
        double source_draw_height = source_height;
        double destination_x = 0;
        double destination_y = 0;
        double destination_width = source_width;
        double destination_height = source_height;
        if (info.Length() >= 9) {
            source_x = info[1]->NumberValue(context).FromMaybe(0);
            source_y = info[2]->NumberValue(context).FromMaybe(0);
            source_draw_width = info[3]->NumberValue(context).FromMaybe(source_width);
            source_draw_height = info[4]->NumberValue(context).FromMaybe(source_height);
            destination_x = info[5]->NumberValue(context).FromMaybe(0);
            destination_y = info[6]->NumberValue(context).FromMaybe(0);
            destination_width = info[7]->NumberValue(context).FromMaybe(source_draw_width);
            destination_height = info[8]->NumberValue(context).FromMaybe(source_draw_height);
        } else if (info.Length() >= 5) {
            destination_x = info[1]->NumberValue(context).FromMaybe(0);
            destination_y = info[2]->NumberValue(context).FromMaybe(0);
            destination_width = info[3]->NumberValue(context).FromMaybe(source_width);
            destination_height = info[4]->NumberValue(context).FromMaybe(source_height);
        } else {
            destination_x = info[1]->NumberValue(context).FromMaybe(0);
            destination_y = info[2]->NumberValue(context).FromMaybe(0);
        }
        if (std::abs(source_draw_width) < 0.001 || std::abs(source_draw_height) < 0.001) return;

        canvas_emit_paint_state(info, *state, canvas_paint_draw_image);
        canvas_append_resource_command(
            *state->node,
            canvas_command_kind::draw_canvas,
            source->id,
            {source_x, source_y, source_draw_width, source_draw_height,
             destination_x, destination_y, destination_width, destination_height});
        state->node->mutable_canvas().commands.back().flags =
            static_cast<uint32_t>(source->canvas().commands.size());
        state->node->mutable_canvas().commands.back().reserved =
            static_cast<uint32_t>(source->canvas().generation);

        const auto source_left = std::min(source_x, source_x + source_draw_width);
        const auto source_top = std::min(source_y, source_y + source_draw_height);
        const auto source_right = std::max(source_x, source_x + source_draw_width);
        const auto source_bottom = std::max(source_y, source_y + source_draw_height);
        const auto scale_x = destination_width / source_draw_width;
        const auto scale_y = destination_height / source_draw_height;
        const auto map_point = [&](double x, double y) {
            return canvas_transform_point(
                *state,
                destination_x + (x - source_x) * scale_x,
                destination_y + (y - source_y) * scale_y);
        };

        for (const auto& rect : source->canvas().rects) {
            const auto clipped_left = std::max<double>(rect.x, source_left);
            const auto clipped_top = std::max<double>(rect.y, source_top);
            const auto clipped_right = std::min<double>(rect.x + rect.width, source_right);
            const auto clipped_bottom = std::min<double>(rect.y + rect.height, source_bottom);
            if (clipped_right <= clipped_left || clipped_bottom <= clipped_top) continue;
            const auto first = map_point(clipped_left, clipped_top);
            const auto second = map_point(clipped_right, clipped_bottom);
            append_canvas_rect(
                *state->node,
                std::min(first.first, second.first),
                std::min(first.second, second.second),
                std::abs(second.first - first.first),
                std::abs(second.second - first.second),
                canvas_apply_global_alpha(
                    rect.rgba,
                    state->emitted_paint_state.global_alpha));
        }

        constexpr size_t maximum_canvas_lines = 200000;
        auto& destination_lines = state->node->mutable_canvas().lines;
        for (const auto& line : source->canvas().lines) {
            const auto left = std::min<double>(line.x1, line.x2);
            const auto top = std::min<double>(line.y1, line.y2);
            const auto right = std::max<double>(line.x1, line.x2);
            const auto bottom = std::max<double>(line.y1, line.y2);
            if (right < source_left || bottom < source_top
                || left > source_right || top > source_bottom) {
                continue;
            }
            if (destination_lines.size() >= maximum_canvas_lines) break;
            const auto first = map_point(line.x1, line.y1);
            const auto second = map_point(line.x2, line.y2);
            destination_lines.push_back(canvas_line_command{
                static_cast<float>(first.first),
                static_cast<float>(first.second),
                static_cast<float>(second.first),
                static_cast<float>(second.second),
                static_cast<float>(
                    line.line_width * std::max(std::abs(scale_x), std::abs(scale_y))),
                canvas_apply_global_alpha(
                    line.rgba,
                    state->emitted_paint_state.global_alpha)});
        }
    }

    static void canvas_fill(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "fill");
        if (state == nullptr || state->node == nullptr) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++state->node->mutable_canvas().fill_calls;
#endif
        const auto fill_rule_index = info.Length() > 0 && info[0]->IsObject() ? 1 : 0;
        const auto even_odd = info.Length() > fill_rule_index
            && info[fill_rule_index]->IsString()
            && to_utf8(info.GetIsolate(), info[fill_rule_index]) == "evenodd";
        if (info.Length() > 0 && info[0]->IsObject()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++state->node->mutable_canvas().path_argument_fill_calls;
#endif
            if (canvas_emit_path_2d(
                    info,
                    *state,
                    canvas_command_kind::fill_svg_path,
                    even_odd)) return;
        }
        if (state->path.empty()) return;
        canvas_emit_paint_state(info, *state, canvas_paint_fill_rect);
        canvas_append_command(*state->node, canvas_command_kind::fill);
        if (even_odd) {
            state->node->mutable_canvas().commands.back().flags |=
                WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD;
        }
        const auto color = canvas_snapshot_color(
            state->emitted_paint_state.fill_style,
            state->emitted_paint_state.global_alpha,
            0x000000FFU);
        constexpr double epsilon = 0.01;

        const auto fill_subpath = [&](size_t begin, size_t end) {
            if (begin >= end) return;
            const auto count = end - begin;
            auto minimum_x = std::numeric_limits<double>::infinity();
            auto minimum_y = std::numeric_limits<double>::infinity();
            auto maximum_x = -std::numeric_limits<double>::infinity();
            auto maximum_y = -std::numeric_limits<double>::infinity();
            bool axis_aligned = true;
            std::vector<std::pair<double, double>> vertices;
            vertices.reserve(count + 1U);
            vertices.emplace_back(state->path[begin].x1, state->path[begin].y1);
            for (auto index = begin; index < end; ++index) {
                const auto& segment = state->path[index];
                minimum_x = std::min({minimum_x, segment.x1, segment.x2});
                minimum_y = std::min({minimum_y, segment.y1, segment.y2});
                maximum_x = std::max({maximum_x, segment.x1, segment.x2});
                maximum_y = std::max({maximum_y, segment.y1, segment.y2});
                axis_aligned = axis_aligned
                    && (std::abs(segment.x1 - segment.x2) <= epsilon
                        || std::abs(segment.y1 - segment.y2) <= epsilon);
                vertices.emplace_back(segment.x2, segment.y2);
            }

            const auto closed = std::abs(vertices.front().first - vertices.back().first) <= epsilon
                && std::abs(vertices.front().second - vertices.back().second) <= epsilon;
            if (count == 4U && closed && axis_aligned) {
                append_canvas_rect(
                    *state->node,
                    minimum_x,
                    minimum_y,
                    maximum_x - minimum_x,
                    maximum_y - minimum_y,
                    color);
                return;
            }

            // Canvas closes open subpaths for fill. Rasterize other polygons into
            // one-device-pixel horizontal spans so arbitrary component fills can
            // still be represented by the probe's immutable rectangle command.
            if (!closed) vertices.push_back(vertices.front());
            const auto first_row = static_cast<int>(std::floor(minimum_y));
            const auto last_row = static_cast<int>(std::ceil(maximum_y));
            if (last_row <= first_row || last_row - first_row > 8192) return;
            std::vector<double> intersections;
            intersections.reserve(vertices.size());
            for (auto row = first_row; row < last_row; ++row) {
                const auto scan_y = static_cast<double>(row) + 0.5;
                intersections.clear();
                for (size_t index = 1; index < vertices.size(); ++index) {
                    const auto [x1, y1] = vertices[index - 1U];
                    const auto [x2, y2] = vertices[index];
                    if ((y1 <= scan_y && y2 > scan_y)
                        || (y2 <= scan_y && y1 > scan_y)) {
                        intersections.push_back(
                            x1 + (scan_y - y1) * (x2 - x1) / (y2 - y1));
                    }
                }
                std::sort(intersections.begin(), intersections.end());
                for (size_t index = 1; index < intersections.size(); index += 2U) {
                    const auto left = intersections[index - 1U];
                    const auto right = intersections[index];
                    if (right > left) {
                        append_canvas_rect(
                            *state->node,
                            left,
                            static_cast<double>(row),
                            right - left,
                            1,
                            color);
                    }
                }
            }
        };

        size_t subpath_begin = 0;
        for (size_t index = 1; index < state->path.size(); ++index) {
            const auto& previous = state->path[index - 1U];
            const auto& current = state->path[index];
            if (std::abs(previous.x2 - current.x1) > epsilon
                || std::abs(previous.y2 - current.y1) > epsilon) {
                fill_subpath(subpath_begin, index);
                subpath_begin = index;
            }
        }
        fill_subpath(subpath_begin, state->path.size());
    }

    static void canvas_clip(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_path));
        auto* state = canvas_path_state(info, "clip");
        if (state == nullptr || state->node == nullptr) return;
        canvas_append_command(*state->node, canvas_command_kind::clip);
        state->has_clip = true;
    }

    static void canvas_fill_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "fillRect");
        if (state == nullptr || state->node == nullptr || info.Length() < 4) return;
        auto& canvas = state->node->mutable_canvas();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++canvas.fill_rect_calls;
#endif
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(0);
        const auto y = info[1]->NumberValue(context).FromMaybe(0);
        const auto width = info[2]->NumberValue(context).FromMaybe(0);
        const auto height = info[3]->NumberValue(context).FromMaybe(0);
        const auto first = canvas_transform_point(*state, x, y);
        const auto second = canvas_transform_point(*state, x + width, y + height);
        const auto transformed_left = std::min(first.first, second.first);
        const auto transformed_top = std::min(first.second, second.second);
        const auto transformed_right = std::max(first.first, second.first);
        const auto transformed_bottom = std::max(first.second, second.second);
        const auto bitmap_width = element_dimension(*state->node, "width", 300);
        const auto bitmap_height = element_dimension(*state->node, "height", 150);
        canvas_emit_paint_state(info, *state, canvas_paint_fill_rect);
        const auto color = canvas_snapshot_color(
            state->emitted_paint_state.fill_style,
            state->emitted_paint_state.global_alpha,
            0x000000FFU);
        const auto& composite =
            state->emitted_paint_state.global_composite_operation;
        const auto saved_clip = std::any_of(
            state->stack.begin(),
            state->stack.end(),
            [](const canvas_snapshot& snapshot) { return snapshot.has_clip; });
        const auto covers_backing_store = transformed_left <= 0
            && transformed_top <= 0
            && transformed_right >= bitmap_width
            && transformed_bottom >= bitmap_height;
        const auto replaces_every_pixel = composite == "copy"
            || (composite == "source-over" && (color & 0xFFU) == 0xFFU);
        if (!state->has_clip && !saved_clip
            && covers_backing_store && replaces_every_pixel) {
            canvas_compact_full_overwrite(info, *state);
        }
        canvas_append_command(
            *state->node,
            canvas_command_kind::fill_rect,
            {x, y, width, height});
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::abs(second.second - bitmap_height) <= 2
            && std::abs(second.first - first.first) >= 1
            && std::abs(second.first - first.first) <= 12
            && std::abs(second.second - first.second) >= 8) {
            ++canvas.probable_volume_fill_rect_calls;
            ++canvas.probable_volume_by_generation[canvas.generation];
        }
        ++canvas.fill_rect_color_calls[color];
#endif
        if (composite == "copy") {
            std::erase_if(canvas.rects, [&](const auto& rect) {
                return rect.x < transformed_right
                    && rect.x + rect.width > transformed_left
                    && rect.y < transformed_bottom
                    && rect.y + rect.height > transformed_top;
            });
            std::erase_if(canvas.lines, [&](const auto& line) {
                const auto left = std::min(line.x1, line.x2);
                const auto right = std::max(line.x1, line.x2);
                const auto top = std::min(line.y1, line.y2);
                const auto bottom = std::max(line.y1, line.y2);
                return left < transformed_right && right > transformed_left
                    && top < transformed_bottom && bottom > transformed_top;
            });
        }
        append_canvas_rect(
            *state->node,
            transformed_left,
            transformed_top,
            transformed_right - transformed_left,
            transformed_bottom - transformed_top,
            color);
    }

    static void canvas_stroke_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "strokeRect");
        if (state == nullptr || state->node == nullptr || info.Length() < 4) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(0);
        const auto y = info[1]->NumberValue(context).FromMaybe(0);
        const auto width = info[2]->NumberValue(context).FromMaybe(0);
        const auto height = info[3]->NumberValue(context).FromMaybe(0);
        canvas_emit_paint_state(info, *state, canvas_paint_stroke_path);
        canvas_append_command(
            *state->node,
            canvas_command_kind::stroke_rect,
            {x, y, width, height});
        const auto line_width = state->emitted_paint_state.line_width;
        const auto color = canvas_snapshot_color(
            state->emitted_paint_state.stroke_style,
            state->emitted_paint_state.global_alpha,
            0x000000FFU);
        const auto top_left = canvas_transform_point(*state, x, y);
        const auto bottom_right = canvas_transform_point(*state, x + width, y + height);
        const auto transformed_x = std::min(top_left.first, bottom_right.first);
        const auto transformed_y = std::min(top_left.second, bottom_right.second);
        const auto transformed_width = std::abs(bottom_right.first - top_left.first);
        const auto transformed_height = std::abs(bottom_right.second - top_left.second);
        append_canvas_rect(*state->node, transformed_x, transformed_y, transformed_width, line_width, color);
        append_canvas_rect(*state->node, transformed_x, transformed_y + transformed_height - line_width, transformed_width, line_width, color);
        append_canvas_rect(*state->node, transformed_x, transformed_y, line_width, transformed_height, color);
        append_canvas_rect(*state->node, transformed_x + transformed_width - line_width, transformed_y, line_width, transformed_height, color);
    }

    static void canvas_clear_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_draw));
        auto* state = canvas_path_state(info, "clearRect");
        if (state == nullptr || state->node == nullptr || info.Length() < 4) return;
        auto& canvas = state->node->mutable_canvas();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++canvas.clear_rect_calls;
#endif
        auto* node = state->node;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto x = info[0]->NumberValue(context).FromMaybe(0);
        const auto y = info[1]->NumberValue(context).FromMaybe(0);
        const auto width = info[2]->NumberValue(context).FromMaybe(0);
        const auto height = info[3]->NumberValue(context).FromMaybe(0);
        const auto first = canvas_transform_point(*state, x, y);
        const auto second = canvas_transform_point(*state, x + width, y + height);
        const auto clear_left = std::min(first.first, second.first);
        const auto clear_top = std::min(first.second, second.second);
        const auto clear_right = std::max(first.first, second.first);
        const auto clear_bottom = std::max(first.second, second.second);
        const auto bitmap_width = element_dimension(*node, "width", 300);
        const auto bitmap_height = element_dimension(*node, "height", 150);
        const auto covers_backing_store = clear_left <= 0 && clear_top <= 0
            && clear_right >= bitmap_width && clear_bottom >= bitmap_height;
        const auto saved_clip = std::any_of(
            state->stack.begin(),
            state->stack.end(),
            [](const canvas_snapshot& snapshot) { return snapshot.has_clip; });
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        canvas.max_clear_stack_depth = std::max<uint64_t>(
            canvas.max_clear_stack_depth,
            state->stack.size());
        if (covers_backing_store) {
            ++canvas.full_clear_calls;
            if (state->has_clip) ++canvas.full_clear_current_clip_calls;
            if (saved_clip) ++canvas.full_clear_saved_clip_calls;
        } else {
            ++canvas.clear_bounds_rejected_calls;
        }
#endif
        if (!state->has_clip && !saved_clip && covers_backing_store) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++canvas.full_clear_reset_calls;
#endif
            // Canvas libraries commonly wrap an entire paint pass in save()/restore().
            // A full backing-store clear still makes every earlier draw command dead,
            // even when that save is active. Rebuild the lightweight state stack so
            // subsequent restores retain browser semantics without retaining every
            // obsolete frame in the display list. A clipped stack is deliberately
            // excluded because the clip path itself cannot yet be reconstructed.
            canvas_compact_full_overwrite(info, *state);
            canvas_append_command(
                *node,
                canvas_command_kind::clear_rect,
                {x, y, width, height});
            return;
        }
        canvas_append_command(
            *node,
            canvas_command_kind::clear_rect,
            {x, y, width, height});
        std::erase_if(canvas.rects, [&](const auto& rect) {
            return rect.x < clear_right && rect.x + rect.width > clear_left
                && rect.y < clear_bottom && rect.y + rect.height > clear_top;
        });
        std::erase_if(canvas.lines, [&](const auto& line) {
            const auto left = std::min(line.x1, line.x2);
            const auto right = std::max(line.x1, line.x2);
            const auto top = std::min(line.y1, line.y2);
            const auto bottom = std::max(line.y1, line.y2);
            return left < clear_right && right > clear_left
                && top < clear_bottom && bottom > clear_top;
        });
    }

    static void canvas_true(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto feature = info.Data().IsEmpty()
            ? std::string("CanvasRenderingContext2D.hitTest")
            : to_utf8(info.GetIsolate(), info.Data());
        self->record_feature(
            "canvas", feature, "unsupported", {}, "native-binding");
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void canvas_set_line_dash(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_state));
        auto* state = canvas_path_state(info);
        if (state == nullptr || state->node == nullptr || info.Length() == 0
            || !info[0]->IsObject()) {
            self->record_feature(
                "canvas",
                "CanvasRenderingContext2D.setLineDash",
                "invalid-authoring",
                "a sequence argument is required",
                "native-binding");
            return;
        }
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto values = info[0].As<v8::Object>();
        v8::Local<v8::Value> length_value;
        if (!values->Get(context, js_string(info.GetIsolate(), "length")).ToLocal(&length_value)) {
            self->record_feature(
                "canvas",
                "CanvasRenderingContext2D.setLineDash",
                "invalid-authoring",
                "the sequence length could not be read",
                "native-binding");
            return;
        }
        const auto length = length_value->Uint32Value(context).FromMaybe(0);
        std::vector<double> segments;
        segments.reserve(length * (length % 2U == 0U ? 1U : 2U));
        for (uint32_t index = 0; index < length; ++index) {
            v8::Local<v8::Value> value;
            if (!values->Get(context, index).ToLocal(&value)) {
                self->record_feature(
                    "canvas",
                    "CanvasRenderingContext2D.setLineDash",
                    "invalid-authoring",
                    "a sequence item could not be read",
                    "native-binding");
                return;
            }
            const auto segment = value->NumberValue(context).FromMaybe(-1);
            if (!std::isfinite(segment) || segment < 0) {
                self->record_feature(
                    "canvas",
                    "CanvasRenderingContext2D.setLineDash",
                    "invalid-authoring",
                    "segments must be finite and non-negative",
                    "native-binding");
                return;
            }
            segments.push_back(segment);
        }
        if (segments.size() % 2U != 0U) {
            const auto original = segments;
            segments.insert(segments.end(), original.begin(), original.end());
        }
        if (segments.size() > 7U) {
            self->record_feature(
                "canvas",
                "CanvasRenderingContext2D.setLineDash",
                "partially-supported",
                "normalized patterns containing at most seven segments",
                "native-binding");
            return;
        }
        state->line_dash = std::move(segments);
        self->record_feature(
            "canvas",
            "CanvasRenderingContext2D.setLineDash",
            "supported",
            {},
            "native-binding");
    }

    static void canvas_get_line_dash(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = canvas_path_state(info, "getLineDash");
        const auto length = state == nullptr ? 0U : static_cast<uint32_t>(state->line_dash.size());
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(length));
        const auto context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < length; ++index) {
            result->Set(
                context,
                index,
                v8::Number::New(info.GetIsolate(), state->line_dash[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    struct canvas_font_description final {
        std::string family{"sans-serif"};
        float size{10.0F};
        int32_t weight{400};
    };

    static canvas_font_description parse_canvas_font(std::string_view shorthand)
    {
        canvas_font_description result;
        const auto first = shorthand.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return result;
        const auto last = shorthand.find_last_not_of(" \t\r\n");
        shorthand = shorthand.substr(first, last - first + 1U);

        size_t size_start = std::string_view::npos;
        size_t px_end = std::string_view::npos;
        for (size_t index = 0; index + 1U < shorthand.size(); ++index) {
            if ((shorthand[index] == 'p' || shorthand[index] == 'P')
                && (shorthand[index + 1U] == 'x' || shorthand[index + 1U] == 'X')) {
                auto number_start = index;
                while (number_start > 0U) {
                    const auto character = shorthand[number_start - 1U];
                    if ((character >= '0' && character <= '9')
                        || character == '.' || character == '+' || character == '-') {
                        --number_start;
                        continue;
                    }
                    break;
                }
                float parsed_size = 0.0F;
                const auto number = shorthand.substr(number_start, index - number_start);
                const auto parse_result = std::from_chars(
                    number.data(),
                    number.data() + number.size(),
                    parsed_size);
                if (parse_result.ec == std::errc{}
                    && parse_result.ptr == number.data() + number.size()
                    && std::isfinite(parsed_size)
                    && parsed_size > 0.0F) {
                    result.size = parsed_size;
                    size_start = number_start;
                    px_end = index + 2U;
                    break;
                }
            }
        }
        if (size_start == std::string_view::npos) return result;

        auto prefix = shorthand.substr(0, size_start);
        size_t token_start = 0U;
        while (token_start < prefix.size()) {
            token_start = prefix.find_first_not_of(" \t\r\n", token_start);
            if (token_start == std::string_view::npos) break;
            auto token_end = prefix.find_first_of(" \t\r\n", token_start);
            if (token_end == std::string_view::npos) token_end = prefix.size();
            auto token = prefix.substr(token_start, token_end - token_start);
            std::string lower(token);
            std::transform(
                lower.begin(), lower.end(), lower.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            if (lower == "bold" || lower == "bolder") {
                result.weight = 700;
            } else if (lower == "lighter") {
                result.weight = 300;
            } else if (lower != "normal") {
                int32_t parsed_weight = 0;
                const auto weight_result = std::from_chars(
                    token.data(), token.data() + token.size(), parsed_weight);
                if (weight_result.ec == std::errc{}
                    && weight_result.ptr == token.data() + token.size()
                    && parsed_weight >= 1 && parsed_weight <= 1000) {
                    result.weight = parsed_weight;
                }
            }
            token_start = token_end;
        }

        auto family_start = shorthand.find_first_not_of(" \t\r\n", px_end);
        if (family_start != std::string_view::npos
            && shorthand[family_start] == '/') {
            ++family_start;
            family_start = shorthand.find_first_not_of(" \t\r\n", family_start);
            if (family_start != std::string_view::npos) {
                family_start = shorthand.find_first_of(" \t\r\n", family_start);
                family_start = shorthand.find_first_not_of(" \t\r\n", family_start);
            }
        }
        if (family_start != std::string_view::npos) {
            result.family = std::string(shorthand.substr(family_start));
        }
        return result;
    }

    static void canvas_measure_text(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "canvas",
            "CanvasRenderingContext2D.measureText",
            "partially-supported",
            "host-backed advance and font bounds; glyph ink bounds are approximate",
            "native-binding");
        const auto text = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        const auto font = canvas_string_property(info, "font", "10px sans-serif");
        const auto parsed_font = parse_canvas_font(font);
        const auto metrics = self->document.measure_text(
            text,
            parsed_font.family,
            parsed_font.size,
            parsed_font.weight);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        const auto set_metric = [&](const char* name, double value) {
            result->Set(
                local_context,
                js_string(info.GetIsolate(), name),
                v8::Number::New(info.GetIsolate(), value)).Check();
        };
        set_metric("width", metrics.advance_width);
        const auto text_align = canvas_string_property(info, "textAlign", "start");
        const auto alignment_offset = text_align == "center"
            ? metrics.advance_width * 0.5
            : text_align == "right" || text_align == "end"
                ? metrics.advance_width
                : 0.0;
        set_metric(
            "actualBoundingBoxLeft",
            metrics.actual_bounding_box_left + alignment_offset);
        set_metric(
            "actualBoundingBoxRight",
            metrics.actual_bounding_box_right - alignment_offset);
        set_metric("fontBoundingBoxAscent", metrics.ascent);
        set_metric("fontBoundingBoxDescent", metrics.descent);
        set_metric("actualBoundingBoxAscent", metrics.actual_bounding_box_ascent);
        set_metric("actualBoundingBoxDescent", metrics.actual_bounding_box_descent);
        set_metric("emHeightAscent", metrics.ascent);
        set_metric("emHeightDescent", metrics.descent);
        set_metric("hangingBaseline", metrics.ascent * 0.8);
        set_metric("alphabeticBaseline", 0.0);
        set_metric("ideographicBaseline", metrics.descent);
        info.GetReturnValue().Set(result);
    }

    static void canvas_gradient(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto feature = info.Data().IsEmpty()
            ? std::string("CanvasRenderingContext2D.createGradient")
            : to_utf8(info.GetIsolate(), info.Data());
        self->record_feature(
            "canvas",
            feature,
            "partially-supported",
            "gradient object construction without paint replay",
            "native-binding");
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "addColorStop"),
            v8::Function::New(
                local_context,
                canvas_no_op,
                js_string(info.GetIsolate(), "CanvasGradient.addColorStop")).ToLocalChecked()).Check();
        info.GetReturnValue().Set(result);
    }

    static void canvas_image_data(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto feature = info.Data().IsEmpty()
            ? std::string("CanvasRenderingContext2D.imageData")
            : to_utf8(info.GetIsolate(), info.Data());
        self->record_feature(
            "canvas",
            feature,
            "partially-supported",
            "shape and zero-initialized pixel storage",
            "native-binding");
        const auto width = info.Length() > 0 ? info[0]->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(1) : 1;
        const auto height = info.Length() > 1 ? info[1]->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(1) : 1;
        const auto byte_count = static_cast<size_t>(std::max(1, width) * std::max(1, height) * 4);
        auto buffer = v8::ArrayBuffer::New(info.GetIsolate(), byte_count);
        auto result = v8::Object::New(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        result->Set(local_context, js_string(info.GetIsolate(), "width"), v8::Integer::New(info.GetIsolate(), width)).Check();
        result->Set(local_context, js_string(info.GetIsolate(), "height"), v8::Integer::New(info.GetIsolate(), height)).Check();
        result->Set(local_context, js_string(info.GetIsolate(), "data"), v8::Uint8ClampedArray::New(buffer, 0, byte_count)).Check();
        info.GetReturnValue().Set(result);
    }

    static void install_canvas_methods(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> result,
        canvas_state& state)
    {
        const char* no_ops[] = {
            "roundRect", "ellipse",
            "putImageData"};
        for (const auto* name : no_ops) {
            const auto feature_name = std::string("CanvasRenderingContext2D.") + name;
            result->Set(
                local_context,
                js_string(isolate, name),
                v8::Function::New(
                    local_context,
                    canvas_no_op,
                    js_string(isolate, feature_name.c_str())).ToLocalChecked()).Check();
        }
        auto path_data = v8::External::New(isolate, &state, v8::kExternalPointerTypeTagDefault);
        result->Set(local_context, js_string(isolate, "fillText"), v8::Function::New(local_context, canvas_text_no_op, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "strokeText"), v8::Function::New(local_context, canvas_stroke_text_no_op, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "fillRect"), v8::Function::New(local_context, canvas_fill_rect, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "strokeRect"), v8::Function::New(local_context, canvas_stroke_rect, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "clearRect"), v8::Function::New(local_context, canvas_clear_rect, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "drawImage"), v8::Function::New(local_context, canvas_draw_image, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "save"), v8::Function::New(local_context, canvas_save, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "restore"), v8::Function::New(local_context, canvas_restore, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "scale"), v8::Function::New(local_context, canvas_scale, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "rotate"), v8::Function::New(local_context, canvas_rotate, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "translate"), v8::Function::New(local_context, canvas_translate, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "transform"), v8::Function::New(local_context, canvas_transform_method, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "setTransform"), v8::Function::New(local_context, canvas_set_transform, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "resetTransform"), v8::Function::New(local_context, canvas_reset_transform, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "beginPath"), v8::Function::New(local_context, canvas_begin_path, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "moveTo"), v8::Function::New(local_context, canvas_move_to, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "lineTo"), v8::Function::New(local_context, canvas_line_to, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "quadraticCurveTo"), v8::Function::New(local_context, canvas_quadratic_curve_to, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "bezierCurveTo"), v8::Function::New(local_context, canvas_bezier_curve_to, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "arc"), v8::Function::New(local_context, canvas_arc, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "arcTo"), v8::Function::New(local_context, canvas_arc_to, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "rect"), v8::Function::New(local_context, canvas_path_rect, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "closePath"), v8::Function::New(local_context, canvas_close_path, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "fill"), v8::Function::New(local_context, canvas_fill, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "stroke"), v8::Function::New(local_context, canvas_stroke, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "clip"), v8::Function::New(local_context, canvas_clip, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "measureText"), v8::Function::New(local_context, canvas_measure_text).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "createLinearGradient"), v8::Function::New(local_context, canvas_gradient, js_string(isolate, "CanvasRenderingContext2D.createLinearGradient")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "createRadialGradient"), v8::Function::New(local_context, canvas_gradient, js_string(isolate, "CanvasRenderingContext2D.createRadialGradient")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "createConicGradient"), v8::Function::New(local_context, canvas_gradient, js_string(isolate, "CanvasRenderingContext2D.createConicGradient")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "setLineDash"), v8::Function::New(local_context, canvas_set_line_dash, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "getLineDash"), v8::Function::New(local_context, canvas_get_line_dash, path_data).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "isPointInPath"), v8::Function::New(local_context, canvas_true, js_string(isolate, "CanvasRenderingContext2D.isPointInPath")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "isPointInStroke"), v8::Function::New(local_context, canvas_true, js_string(isolate, "CanvasRenderingContext2D.isPointInStroke")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "createImageData"), v8::Function::New(local_context, canvas_image_data, js_string(isolate, "CanvasRenderingContext2D.createImageData")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "getImageData"), v8::Function::New(local_context, canvas_image_data, js_string(isolate, "CanvasRenderingContext2D.getImageData")).ToLocalChecked()).Check();
        result->Set(local_context, js_string(isolate, "canvas"), v8::Null(isolate)).Check();
        result->Set(local_context, js_string(isolate, "font"), js_string(isolate, "10px sans-serif")).Check();
        result->Set(local_context, js_string(isolate, "fillStyle"), js_string(isolate, "#000000")).Check();
        result->Set(local_context, js_string(isolate, "strokeStyle"), js_string(isolate, "#000000")).Check();
        result->Set(local_context, js_string(isolate, "lineWidth"), v8::Number::New(isolate, 1)).Check();
        result->Set(local_context, js_string(isolate, "globalAlpha"), v8::Number::New(isolate, 1)).Check();
        result->Set(
            local_context,
            js_string(isolate, "globalCompositeOperation"),
            js_string(isolate, "source-over")).Check();
    }

    static void canvas_get_context(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::canvas_state));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || node->tag != "canvas") {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto requested = info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        if (requested != "2d") {
            self->record_feature(
                "canvas",
                "HTMLCanvasElement.getContext:" + (requested.empty() ? std::string("missing") : requested),
                "unsupported",
                {},
                "native-binding");
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        self->record_feature(
            "canvas",
            "HTMLCanvasElement.getContext:2d",
            "supported",
            {},
            "native-binding");
        const auto key = self->wrapper_key(*node);
        const auto known = self->canvas_contexts.find(key);
        if (known != self->canvas_contexts.end() && !known->second.IsEmpty()) {
            if (self->is_connected(*node) && known->second.IsWeak()) {
                known->second.ClearWeak();
            }
            info.GetReturnValue().Set(known->second.Get(info.GetIsolate()));
            return;
        }
        if (known != self->canvas_contexts.end()) {
            self->canvas_contexts.erase(known);
            self->canvas_states.erase(key);
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        auto state = std::make_unique<canvas_state>();
        state->node = node;
        auto* state_pointer = state.get();
        self->canvas_states[key] = std::move(state);
        install_canvas_methods(info.GetIsolate(), local_context, result, *state_pointer);
        result->Set(local_context, js_string(info.GetIsolate(), "canvas"), info.This()).Check();
        auto& wrapper = self->canvas_contexts[key];
        wrapper.Reset(info.GetIsolate(), result);
        if (!self->is_connected(*node)) wrapper.SetWeak();
        info.GetReturnValue().Set(result);
    }

    static double object_number(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name,
        double fallback)
    {
        v8::Local<v8::Value> value;
        if (!object->Get(context, js_string(isolate, name)).ToLocal(&value)
            || !value->IsNumber()) return fallback;
        const auto result = value->NumberValue(context).FromMaybe(fallback);
        return std::isfinite(result) ? result : fallback;
    }

    static void set_object_number(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name,
        double value)
    {
        object->Set(
            context,
            js_string(isolate, name),
            v8::Number::New(isolate, value)).Check();
    }

    static void path_2d_add_path(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0 || !info[0]->IsObject()) return;
        current(info.GetIsolate())->record_feature(
            "canvas", "Path2D.addPath", "supported", {}, "native-binding");
        const auto isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::Value> destination_value;
        v8::Local<v8::Value> source_value;
        if (!info.This()->Get(
                context,
                js_string(isolate, "__websceneSvgPathEntries")).ToLocal(&destination_value)
            || !destination_value->IsArray()
            || !info[0].As<v8::Object>()->Get(
                context,
                js_string(isolate, "__websceneSvgPathEntries")).ToLocal(&source_value)
            || !source_value->IsArray()) return;
        const auto destination = destination_value.As<v8::Array>();
        const auto source = source_value.As<v8::Array>();
        auto matrix = v8::Object::New(isolate);
        if (info.Length() > 1 && info[1]->IsObject()) matrix = info[1].As<v8::Object>();
        const auto ma = object_number(isolate, context, matrix, "a", 1);
        const auto mb = object_number(isolate, context, matrix, "b", 0);
        const auto mc = object_number(isolate, context, matrix, "c", 0);
        const auto md = object_number(isolate, context, matrix, "d", 1);
        const auto me = object_number(isolate, context, matrix, "e", 0);
        const auto mf = object_number(isolate, context, matrix, "f", 0);
        auto destination_index = destination->Length();
        const auto source_length = source->Length();
        for (uint32_t index = 0; index < source_length; ++index) {
            v8::Local<v8::Value> source_entry_value;
            if (!source->Get(context, index).ToLocal(&source_entry_value)
                || !source_entry_value->IsObject()) continue;
            const auto source_entry = source_entry_value.As<v8::Object>();
            v8::Local<v8::Value> data;
            if (!source_entry->Get(context, js_string(isolate, "data")).ToLocal(&data)
                || !data->IsString()) continue;
            const auto sa = object_number(isolate, context, source_entry, "a", 1);
            const auto sb = object_number(isolate, context, source_entry, "b", 0);
            const auto sc = object_number(isolate, context, source_entry, "c", 0);
            const auto sd = object_number(isolate, context, source_entry, "d", 1);
            const auto se = object_number(isolate, context, source_entry, "e", 0);
            const auto sf = object_number(isolate, context, source_entry, "f", 0);
            const auto entry = v8::Object::New(isolate);
            entry->Set(context, js_string(isolate, "data"), data).Check();
            set_object_number(isolate, context, entry, "a", ma * sa + mc * sb);
            set_object_number(isolate, context, entry, "b", mb * sa + md * sb);
            set_object_number(isolate, context, entry, "c", ma * sc + mc * sd);
            set_object_number(isolate, context, entry, "d", mb * sc + md * sd);
            set_object_number(isolate, context, entry, "e", ma * se + mc * sf + me);
            set_object_number(isolate, context, entry, "f", mb * se + md * sf + mf);
            destination->Set(context, destination_index++, entry).Check();
        }
    }

    static void path_2d_arc(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (info.Length() < 5) return;
        const auto isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        const auto center_x = info[0]->NumberValue(context).FromMaybe(0);
        const auto center_y = info[1]->NumberValue(context).FromMaybe(0);
        const auto radius = info[2]->NumberValue(context).FromMaybe(-1);
        const auto start = info[3]->NumberValue(context).FromMaybe(0);
        const auto authored_end = info[4]->NumberValue(context).FromMaybe(0);
        const auto anticlockwise = info.Length() > 5 && info[5]->BooleanValue(isolate);
        if (!std::isfinite(center_x) || !std::isfinite(center_y)
            || !std::isfinite(radius) || !std::isfinite(start)
            || !std::isfinite(authored_end)) return;
        if (radius < 0) {
            isolate->ThrowException(v8::Exception::RangeError(
                js_string(isolate, "Path2D.arc radius must be non-negative")));
            return;
        }
        self->record_feature(
            "canvas",
            "Path2D.arc",
            "supported",
            {},
            "native-binding");
        if (radius == 0) return;

        v8::Local<v8::Value> entries_value;
        if (!info.This()->Get(
                context,
                js_string(isolate, "__websceneSvgPathEntries")).ToLocal(&entries_value)
            || !entries_value->IsArray()) return;
        constexpr double full_circle = 6.28318530717958647692;
        constexpr double half_circle = full_circle * 0.5;
        const auto authored_delta = authored_end - start;
        const auto full = std::abs(authored_delta) >= full_circle;
        auto end = authored_end;
        if (full) {
            end = start + (anticlockwise ? -full_circle : full_circle);
        } else if (anticlockwise) {
            while (end > start) end -= full_circle;
            if (start - end > full_circle) end = start - full_circle;
        } else {
            while (end < start) end += full_circle;
            if (end - start > full_circle) end = start + full_circle;
        }
        const auto start_x = center_x + std::cos(start) * radius;
        const auto start_y = center_y + std::sin(start) * radius;
        const auto sweep = anticlockwise ? 0 : 1;
        std::ostringstream data;
        data << std::setprecision(std::numeric_limits<double>::max_digits10)
             << "M " << start_x << ' ' << start_y << ' ';
        if (full) {
            const auto middle_angle = start + (anticlockwise ? -half_circle : half_circle);
            const auto middle_x = center_x + std::cos(middle_angle) * radius;
            const auto middle_y = center_y + std::sin(middle_angle) * radius;
            data << "A " << radius << ' ' << radius << " 0 1 " << sweep << ' '
                 << middle_x << ' ' << middle_y << ' '
                 << "A " << radius << ' ' << radius << " 0 1 " << sweep << ' '
                 << start_x << ' ' << start_y;
        } else if (end != start) {
            const auto end_x = center_x + std::cos(end) * radius;
            const auto end_y = center_y + std::sin(end) * radius;
            const auto large_arc = std::abs(end - start) > half_circle ? 1 : 0;
            data << "A " << radius << ' ' << radius << " 0 " << large_arc << ' '
                 << sweep << ' ' << end_x << ' ' << end_y;
        } else {
            return;
        }
        auto entry = v8::Object::New(isolate);
        entry->Set(
            context,
            js_string(isolate, "data"),
            js_string(isolate, data.str().c_str())).Check();
        const auto entries = entries_value.As<v8::Array>();
        entries->Set(context, entries->Length(), entry).Check();
    }

    static void path_2d_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        current(info.GetIsolate())->record_feature(
            "canvas", "Path2D.constructor", "supported", {}, "native-binding");
        auto result = info.IsConstructCall() ? info.This() : v8::Object::New(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto entries = v8::Array::New(info.GetIsolate());
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "__websceneSvgPathEntries"),
            entries).Check();
        const char* methods[] = {
            "closePath", "moveTo", "lineTo", "bezierCurveTo", "quadraticCurveTo",
            "arcTo", "ellipse", "rect", "roundRect"};
        for (const auto* name : methods) {
            const auto feature_name = std::string("Path2D.") + name;
            result->Set(
                local_context,
                js_string(info.GetIsolate(), name),
                v8::Function::New(
                    local_context,
                    canvas_no_op,
                    js_string(info.GetIsolate(), feature_name.c_str())).ToLocalChecked()).Check();
        }
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "arc"),
            v8::Function::New(local_context, path_2d_arc).ToLocalChecked()).Check();
        result->Set(
            local_context,
            js_string(info.GetIsolate(), "addPath"),
            v8::Function::New(local_context, path_2d_add_path).ToLocalChecked()).Check();
        if (info.Length() > 0 && info[0]->IsString()) {
            auto entry = v8::Object::New(info.GetIsolate());
            entry->Set(
                local_context,
                js_string(info.GetIsolate(), "data"),
                info[0]).Check();
            entries->Set(local_context, 0, entry).Check();
            result->Set(
                local_context,
                js_string(info.GetIsolate(), "__websceneSvgPathData"),
                info[0]).Check();
        } else if (info.Length() > 0 && info[0]->IsObject()) {
            v8::Local<v8::Value> source_entries_value;
            if (info[0].As<v8::Object>()->Get(
                    local_context,
                    js_string(info.GetIsolate(), "__websceneSvgPathEntries")).ToLocal(&source_entries_value)
                && source_entries_value->IsArray()) {
                const auto source_entries = source_entries_value.As<v8::Array>();
                for (uint32_t index = 0; index < source_entries->Length(); ++index) {
                    v8::Local<v8::Value> entry;
                    if (source_entries->Get(local_context, index).ToLocal(&entry)) {
                        entries->Set(local_context, index, entry).Check();
                    }
                }
            }
        }
        info.GetReturnValue().Set(result);
    }
