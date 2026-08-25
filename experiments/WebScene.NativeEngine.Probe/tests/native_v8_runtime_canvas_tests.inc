struct canvas_text_measurement_probe final {
    std::string text;
    std::string family;
    float size{0};
    int32_t weight{0};
    size_t calls{0};
};

uint8_t measure_canvas_fixture_text(
    void* user_data,
    const char* text,
    size_t text_length,
    const char* font_family,
    size_t font_family_length,
    float font_size,
    int32_t font_weight,
    float,
    float,
    webscene_text_metrics* metrics)
{
    if (user_data == nullptr || text == nullptr || font_family == nullptr
        || metrics == nullptr
        || metrics->struct_size < sizeof(webscene_text_metrics)) {
        return 0U;
    }
    auto& probe = *static_cast<canvas_text_measurement_probe*>(user_data);
    probe.text.assign(text, text_length);
    probe.family.assign(font_family, font_family_length);
    probe.size = font_size;
    probe.weight = font_weight;
    ++probe.calls;
    metrics->advance_width = static_cast<float>(text_length) * font_size * 0.5F
        + static_cast<float>(font_weight) * 0.001F;
    metrics->ascent = font_size * 0.75F;
    metrics->descent = font_size * 0.25F;
    metrics->leading = font_size * 0.1F;
    metrics->actual_bounding_box_left = 1.0F;
    metrics->actual_bounding_box_right = metrics->advance_width - 2.0F;
    metrics->actual_bounding_box_ascent = font_size * 0.6F;
    metrics->actual_bounding_box_descent = font_size * 0.15F;
    return 1U;
}

void test_canvas_text_metrics_use_host_font_axes()
{
    canvas_text_measurement_probe probe;
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.text_measure_callback = measure_canvas_fixture_text;
    options.text_measure_user_data = &probe;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "canvas text-metrics engine creation failed");

    const auto result = evaluate(engine, R"JS(
        (() => {
          const context = document.createElement('canvas').getContext('2d');
          context.font = '10px sans-serif';
          const small = context.measureText('MMMM');
          context.font = '20px sans-serif';
          const large = context.measureText('MMMM');
          context.font = "bold 12px -apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif";
          const bold = context.measureText('Label');
          context.textBaseline = 'middle';
          const middle = context.measureText('Label');
          return {
            defaultFieldsAreFinite: [
              bold.width,
              bold.actualBoundingBoxAscent,
              bold.actualBoundingBoxDescent,
              bold.fontBoundingBoxAscent,
              bold.fontBoundingBoxDescent
            ].every(Number.isFinite),
            sizeChangesAdvance: large.width > small.width * 1.9,
            ascent: bold.actualBoundingBoxAscent,
            descent: bold.actualBoundingBoxDescent,
            fontAscent: bold.fontBoundingBoxAscent,
            fontDescent: bold.fontBoundingBoxDescent,
            middleAscent: middle.actualBoundingBoxAscent,
            middleDescent: middle.actualBoundingBoxDescent,
            middleFontAscent: middle.fontBoundingBoxAscent,
            middleFontDescent: middle.fontBoundingBoxDescent,
            middleAlphabeticBaseline: middle.alphabeticBaseline
          };
        })()
    )JS", "native-canvas-host-text-metrics.js");

    require(
        result.find("\"defaultFieldsAreFinite\":true") != std::string::npos
            && result.find("\"sizeChangesAdvance\":true") != std::string::npos,
        "Canvas TextMetrics did not use finite host font metrics: " + result);
    require(
        result.find("\"ascent\":7.2") != std::string::npos
            && result.find("\"descent\":1.8") != std::string::npos
            && result.find("\"fontAscent\":9") != std::string::npos
            && result.find("\"fontDescent\":3") != std::string::npos,
        "Canvas TextMetrics did not distinguish glyph ink from font bounds: " + result);
    require(
        result.find("\"middleAscent\":4.2") != std::string::npos
            && result.find("\"middleDescent\":4.8") != std::string::npos
            && result.find("\"middleFontAscent\":6") != std::string::npos
            && result.find("\"middleFontDescent\":6") != std::string::npos
            && result.find("\"middleAlphabeticBaseline\":3") != std::string::npos,
        "Canvas TextMetrics did not resolve distances from the active middle baseline: "
            + result);
    require(
        probe.calls == 3U,
        "Canvas measureText did not reuse host metrics across baseline-only changes");
    require(probe.text == "Label", "Canvas measureText changed the measured text");
    require(
        probe.family == "-apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif",
        "Canvas measureText lost the authored fallback family list: " + probe.family);
    require(std::abs(probe.size - 12.0F) < 0.001F, "Canvas measureText lost the px font size");
    require(probe.weight == 700, "Canvas measureText lost the bold font weight");

    webscene_engine_destroy(engine);
}

void test_canvas_text_max_width_is_preserved_in_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '<canvas id="labels" width="200" height="80"></canvas>';
          const context = document.getElementById('labels').getContext('2d');
          context.font = '20px sans-serif';
          context.fillText('compressed fill', 80, 24, 37);
          context.strokeText('natural stroke', 80, 52);
        })()
    )JS", "native-canvas-text-max-width-setup.js");
    webscene_engine_request_scene_checkpoint(engine);

    auto observed_fill = false;
    auto observed_natural_stroke = false;
    for (auto attempt = 0; attempt < 100
        && (!observed_fill || !observed_natural_stroke); ++attempt) {
        if (const auto* scene = webscene_engine_acquire_latest_scene(engine); scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 25U) {
                    observed_fill =
                        (command.flags & WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH) != 0U
                        && std::abs(command.data.values[2] - 37.0) < 0.001;
                } else if (command.kind == 26U) {
                    observed_natural_stroke =
                        (command.flags & WEBSCENE_CANVAS_COMMAND_FLAG_TEXT_MAX_WIDTH) == 0U;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_fill || !observed_natural_stroke) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(observed_fill, "Canvas fillText maxWidth was lost before scene publication");
    require(
        observed_natural_stroke,
        "Canvas strokeText without maxWidth incorrectly requested condensation");
}

void test_canvas_path_even_odd_fill_rule_reaches_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 40;
          canvas.height = 40;
          canvas.style.width = '40px';
          canvas.style.height = '40px';
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.beginPath();
          context.rect(2, 2, 36, 36);
          context.rect(12, 12, 16, 16);
          context.fillStyle = '#ffffff';
          context.fill('evenodd');
        })()
    )JS", "native-canvas-even-odd-setup.js");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_fill = false;
    bool observed_even_odd = false;
    std::string observed_commands;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                observed_commands += std::to_string(command.kind) + ":"
                    + std::to_string(command.flags) + ",";
                if (command.kind == 21U) {
                    observed_fill = true;
                    observed_even_odd =
                        (command.flags & WEBSCENE_CANVAS_COMMAND_FLAG_EVEN_ODD) != 0U;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_even_odd) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_fill) {
        fail("canvas fill was not emitted to the native scene; commands="
            + observed_commands);
    }
    require(observed_even_odd, "canvas even-odd fill rule was lost before the native scene");
}

void test_canvas_fill_rect_emits_only_relevant_paint_state(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 40;
          canvas.height = 40;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.fillStyle = '#204060';
          context.globalAlpha = 0.5;
          context.lineWidth = 7;
          context.strokeStyle = '#80a0c0';
          context.fillRect(1, 2, 30, 20);
          context.strokeRect(3, 4, 12, 8);
        })()
    )JS", "native-canvas-fill-rect-state-slice.js");
    webscene_engine_request_scene_checkpoint(engine);

    auto observed_fill_rect = false;
    auto observed_unrelated_fill_state = false;
    auto observed_stroke_style = false;
    auto observed_line_width = false;
    auto observed_stroke_rect = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto kind = scene->canvas_commands[index].kind;
                if (!observed_fill_rect) {
                    if (kind == 22U) {
                        observed_fill_rect = true;
                    } else if (kind == 41U || kind == 42U
                        || kind == 43U || kind == 44U || kind == 45U
                        || kind == 47U || kind == 48U || kind == 49U
                        || kind == 50U || kind == 51U || kind == 52U) {
                        observed_unrelated_fill_state = true;
                    }
                } else {
                    if (kind == 41U) observed_stroke_style = true;
                    if (kind == 42U
                        && std::abs(scene->canvas_commands[index].data.values[0] - 7.0)
                            < 0.001) {
                        observed_line_width = true;
                    }
                    if (kind == 23U) observed_stroke_rect = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_stroke_rect) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    require(observed_fill_rect, "Canvas2D fillRect was absent from the scene packet");
    require(
        !observed_unrelated_fill_state,
        "Canvas2D fillRect eagerly emitted unrelated stroke, line, text, or image state");
    require(
        observed_stroke_style && observed_line_width && observed_stroke_rect,
        "deferred Canvas2D stroke state was not emitted before the later strokeRect");
}

void test_canvas_path_2d_add_path_does_not_fill_stale_current_path(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 80;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.beginPath();
          context.rect(0, 0, 22, 19);
          context.clip();
          const source = new Path2D('M20 20h40v40H20z');
          const scaled = new Path2D();
          const identity = new DOMMatrixReadOnly();
          const transform = identity.translate(3, 4).scale(0.5, 0.25).translate(6, 8);
          scaled.addPath(source, transform);
          globalThis.__pathMatrixState = [
            identity.a, identity.d, identity.e, identity.f,
            transform.a, transform.d, transform.e, transform.f
          ];
          context.fillStyle = '#1e53e5';
          context.setTransform(transform);
          context.fill(source);
          context.resetTransform();
          context.fill(scaled);
        })()
    )JS", "native-canvas-path2d-add-path.js");
    require(
        evaluate(engine, "globalThis.__pathMatrixState", "native-canvas-path2d-matrix.js")
            == "[1,1,0,0,0.5,0.25,6,6]",
        "DOMMatrixReadOnly transform chaining mutated its source or lost translation");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_svg_fill = false;
    bool observed_stale_fill = false;
    bool observed_transform = false;
    bool observed_object_set_transform = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 21U) observed_stale_fill = true;
                if (command.kind == 4U
                    && std::abs(command.data.values[0] - 0.5) < 0.001
                    && std::abs(command.data.values[3] - 0.25) < 0.001
                    && std::abs(command.data.values[4] - 6.0) < 0.001
                    && std::abs(command.data.values[5] - 6.0) < 0.001) {
                    observed_object_set_transform = true;
                }
                if (command.kind == 28U) {
                    observed_svg_fill = true;
                    observed_transform = (command.flags & 0xFFFFU) == 6U
                        && std::abs(command.data.values[0] - 0.5) < 0.001
                        && std::abs(command.data.values[3] - 0.25) < 0.001
                        && std::abs(command.data.values[4] - 6.0) < 0.001
                        && std::abs(command.data.values[5] - 6.0) < 0.001;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_svg_fill) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_svg_fill || !observed_transform
        || !observed_object_set_transform || observed_stale_fill) {
        fail("Path2D.addPath fill did not preserve its transform independently of the current path: svg="
            + std::to_string(observed_svg_fill) + ", transform="
            + std::to_string(observed_transform) + ", object-set-transform="
            + std::to_string(observed_object_set_transform) + ", stale="
            + std::to_string(observed_stale_fill));
    }
}

void test_canvas_line_dash_and_path_2d_arc_are_native(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 80;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.setLineDash([2, 3, 4]);
          const dash = context.getLineDash();
          context.setLineDash([2, -1]);
          const retained = context.getLineDash();
          const path = new Path2D();
          path.arc(40, 40, 20, 0, Math.PI * 2);
          context.stroke(path);
          context.beginPath();
          context.rect(0, 0, 40, 40);
          context.rect(10, 10, 20, 20);
          const nonzero = context.isPointInPath(20, 20);
          const evenodd = context.isPointInPath(20, 20, 'evenodd');
          context.beginPath();
          context.moveTo(5, 60);
          context.lineTo(70, 60);
          context.lineWidth = 6;
          const strokeHit = context.isPointInStroke(40, 62);
          const strokeMiss = context.isPointInStroke(40, 68);
          return { dash, retained, nonzero, evenodd, strokeHit, strokeMiss };
        })()
    )JS", "native-canvas-line-dash-path2d-arc.js");
    require(
        result == R"({"dash":[2,3,4,2,3,4],"retained":[2,3,4,2,3,4],"nonzero":true,"evenodd":false,"strokeHit":true,"strokeMiss":false})",
        "Canvas2D line-dash or current-path hit testing regressed: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    bool observed_dash = false;
    bool observed_arc_path = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 19U) {
                    observed_dash = command.flags == 7U
                        && command.data.values[0] == 6
                        && command.data.values[1] == 2
                        && command.data.values[2] == 3
                        && command.data.values[3] == 4
                        && command.data.values[4] == 2
                        && command.data.values[5] == 3
                        && command.data.values[6] == 4;
                }
                if (command.kind == 29U) observed_arc_path = true;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_dash && observed_arc_path) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(observed_dash, "Canvas2D setLineDash did not reach the native scene packet");
    require(observed_arc_path, "Path2D.arc did not reach native SVG-path replay");

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"CanvasRenderingContext2D.setLineDash","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"Path2D.arc","classification":"supported")")
                != std::string::npos,
        "supported canvas feature decisions were absent from the feature inventory: " + report);
#endif
}

void test_canvas_ellipse_records_complete_path_command(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const canvas = document.createElement('canvas');
          canvas.width = 320;
          canvas.height = 180;
          document.body.appendChild(canvas);
          const context = canvas.getContext('2d');
          context.beginPath();
          context.moveTo(52, 136);
          context.lineTo(268, 32);
          context.stroke();
          context.beginPath();
          context.ellipse(52, 136, 8, 6, 0.25, 0, Math.PI * 2, true);
          context.stroke();
          const path = new Path2D();
          path.ellipse(268, 32, 8, 6, -0.25, 0, Math.PI * 2);
          context.stroke(path);
          let negativeRadius = false;
          try { context.ellipse(0, 0, -1, 2, 0, 0, 1); }
          catch (error) { negativeRadius = error instanceof DOMException && error.name === 'IndexSizeError'; }
          context.beginPath();
          context.roundRect(10, 10, 40, 20, [{x: 8, y: 6}, 4]);
          const roundRectCenter = context.isPointInPath(30, 20);
          const roundRectCorner = context.isPointInPath(10, 10);
          let negativeRoundRect = false;
          try { context.roundRect(0, 0, 10, 10, [2, -1]); }
          catch (error) { negativeRoundRect = error instanceof RangeError; }
          return { negativeRadius, negativeRoundRect, roundRectCenter, roundRectCorner };
        })()
    )JS", "native-canvas-ellipse-trendline.js");
    require(
        result == R"({"negativeRadius":true,"negativeRoundRect":true,"roundRectCenter":true,"roundRectCorner":false})",
        "Canvas2D ellipse or roundRect validation/geometry regressed: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    bool observed_baseline_line = false;
    bool observed_ellipse = false;
    bool observed_path_2d_ellipse = false;
    uint32_t observed_strokes = 0;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 12U
                    && std::abs(command.data.values[0] - 268.0) < 0.001
                    && std::abs(command.data.values[1] - 32.0) < 0.001) {
                    observed_baseline_line = true;
                }
                if (command.kind == 30U
                    && command.flags == 8U
                    && std::abs(command.data.values[0] - 52.0) < 0.001
                    && std::abs(command.data.values[1] - 136.0) < 0.001
                    && std::abs(command.data.values[2] - 8.0) < 0.001
                    && std::abs(command.data.values[3] - 6.0) < 0.001
                    && std::abs(command.data.values[4] - 0.25) < 0.001
                    && std::abs(command.data.values[5]) < 0.001
                    && std::abs(command.data.values[6] - 6.283185307179586) < 0.001
                    && command.data.values[7] == 1.0) {
                    observed_ellipse = true;
                }
                if (command.kind == 29U) observed_path_2d_ellipse = true;
                if (command.kind == 20U) ++observed_strokes;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_baseline_line && observed_ellipse
            && observed_path_2d_ellipse && observed_strokes >= 2U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        observed_baseline_line && observed_strokes >= 2U,
        "Canvas2D baseline trendline did not reach the scene packet");
    require(
        observed_ellipse,
        "Canvas2D ellipse did not retain its complete geometry and direction");
    require(
        observed_path_2d_ellipse,
        "Path2D ellipse did not reach native SVG-path replay");

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"CanvasRenderingContext2D.ellipse","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"Path2D.ellipse","classification":"partially-supported")")
                != std::string::npos,
        "supported ellipse path was absent from native feature telemetry: " + report);
#endif
}

size_t count_canvas_layouts_with_bitmap(
    webscene_engine* engine,
    uint32_t bitmap_width,
    uint32_t bitmap_height)
{
    const auto required = webscene_engine_copy_canvas_layouts(engine, nullptr, 0);
    std::vector<webscene_canvas_layout> layouts(required);
    const auto copied = required == 0
        ? 0U
        : webscene_engine_copy_canvas_layouts(engine, layouts.data(), layouts.size());
    return static_cast<size_t>(std::count_if(
        layouts.begin(),
        layouts.begin() + static_cast<std::ptrdiff_t>(copied),
        [&](const auto& layout) {
            return layout.bitmap_width == bitmap_width
                && layout.bitmap_height == bitmap_height;
        }));
}

bool wait_for_canvas_layout_count(
    webscene_engine* engine,
    uint32_t bitmap_width,
    uint32_t bitmap_height,
    size_t expected)
{
    webscene_engine_request_scene_checkpoint(engine);
    for (auto attempt = 0; attempt < 250; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (count_canvas_layouts_with_bitmap(engine, bitmap_width, bitmap_height) == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void test_detached_canvas_descendants_leave_native_scene(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          host.id = 'detached-canvas-host';
          const first = document.createElement('canvas');
          first.width = 173;
          first.height = 61;
          first.style.width = '173px';
          first.style.height = '61px';
          first.getContext('2d').fillRect(0, 0, 173, 61);
          const second = document.createElement('canvas');
          second.width = 173;
          second.height = 62;
          second.style.width = '173px';
          second.style.height = '62px';
          second.getContext('2d').fillRect(0, 0, 173, 62);
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);
        })();
    )JS", "native-detached-canvas-setup.js");
    require(
        wait_for_canvas_layout_count(engine, 173U, 61U, 1U)
            && wait_for_canvas_layout_count(engine, 173U, 62U, 1U),
        "connected descendant canvases did not enter the native scene");

    const auto state = evaluate(engine, R"JS(
        (() => {
          const host = document.querySelector('#detached-canvas-host');
          const canvases = [...host.querySelectorAll('canvas')];
          host.remove();
          return {
            hostParent: host.parentNode,
            childParentsRetained: canvases.every(canvas => canvas.parentNode === host),
            queryInvisible: document.querySelectorAll('#detached-canvas-host canvas').length === 0
          };
        })()
    )JS", "native-detached-canvas-remove.js");
    require(
        state.find(R"("hostParent":null)") != std::string::npos
            && state.find(R"("childParentsRetained":true)") != std::string::npos
            && state.find(R"("queryInvisible":true)") != std::string::npos,
        "canvas subtree did not retain detached DOM identity: " + state);
    require(
        wait_for_canvas_layout_count(engine, 173U, 61U, 0U)
            && wait_for_canvas_layout_count(engine, 173U, 62U, 0U),
        "canvas descendants of a detached subtree remained in the native scene");
}

void test_svg_dom_parser_preserves_fill_rule(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write(
            '<html><body><script>'
            + 'const parsed = new DOMParser().parseFromString('
            + '`<svg viewBox="0 0 10 10"><path fill="currentColor" fill-rule="evenodd" d="M0 0h10v10H0z"/></svg>`,'
            + '`image/svg+xml`);'
            + 'globalThis.__parsedFillRule = parsed.querySelector(`path`).getAttribute(`fill-rule`);'
            + 'try {'
            + '  parsed.firstElementChild.setAttribute(`color`, `#2962ff`);'
            + '  const serialized = (new XMLSerializer).serializeToString(parsed);'
            + '  globalThis.__serializedSvg = serialized;'
            + '  const image = new Image();'
            + '  globalThis.__serializedSvgImageState = `pending`;'
            + '  image.onload = () => globalThis.__serializedSvgImageState = `loaded`;'
            + '  image.onerror = () => globalThis.__serializedSvgImageState = `error`;'
            + '  image.src = URL.createObjectURL(new Blob([serialized], { type: `image/svg+xml` }));'
            + '} catch (error) {'
            + '  globalThis.__serializedSvg = `${typeof XMLSerializer}|${error.stack || error}`;'
            + '  globalThis.__serializedSvgImageState = `exception`;'
            + '}'
            + '</script></body></html>');
          frameDocument.close();
          globalThis.__fillRuleFrame = frame;
        })()
    )JS", "native-svg-fill-rule-setup.js");
    const auto parsed_fill_rule = evaluate(engine,
        "globalThis.__fillRuleFrame.contentWindow.__parsedFillRule",
        "native-svg-fill-rule-result.js");
    if (parsed_fill_rule != "\"evenodd\"") {
        fail("DOMParser discarded an SVG fill-rule attribute: " + parsed_fill_rule);
    }
    const auto serialized_svg = evaluate(engine,
        "globalThis.__fillRuleFrame.contentWindow.__serializedSvg",
        "native-svg-serialization-result.js");
    if (serialized_svg.find("<svg") == std::string::npos
        || serialized_svg.find(R"(color=\"#2962ff\")") == std::string::npos
        || serialized_svg.find(R"(fill-rule=\"evenodd\")") == std::string::npos) {
        fail("XMLSerializer did not preserve the TradingView SVG image payload: "
            + serialized_svg);
    }
    auto image_state = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        image_state = evaluate(engine,
            "globalThis.__fillRuleFrame.contentWindow.__serializedSvgImageState",
            "native-svg-object-url-image-result.js");
        if (image_state != R"("pending")") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (image_state != R"("loaded")") {
        fail("serialized SVG Blob did not dispatch Image.onload: " + image_state);
    }
}

void test_compound_root_selector_applies_dark_custom_palette(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.documentElement.classList.add('theme-dark');
          const surface = document.createElement('div');
          surface.className = 'surface';
          document.body.appendChild(surface);
          const style = document.createElement('style');
          style.textContent = `
            :root { --custom-surface: #252728; }
            .theme-dark:root { --custom-surface: #0b181a; }
            .surface { background-color: var(--custom-surface); width: 10px; height: 10px; }
          `;
          document.body.appendChild(style);
          return getComputedStyle(surface).backgroundColor;
        })()
    )JS", "native-compound-root-custom-palette.js");
    if (result != "\"rgb(11, 24, 26)\"") {
        fail("compound :root selector did not override the custom palette: " + result);
    }
}

void test_adjacent_inline_runs_share_wrapped_lines(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const paragraph = document.createElement('p');
          paragraph.style.width = '288px';
          paragraph.style.fontSize = '14px';
          paragraph.style.lineHeight = '21px';
          const normal = document.createElement('span');
          normal.appendChild(document.createTextNode("All's well — market is open."));
          const bold = document.createElement('span');
          bold.style.fontWeight = '700';
          bold.appendChild(document.createTextNode("It'll close in 16 hours and 19 minutes."));
          paragraph.appendChild(normal);
          paragraph.appendChild(bold);
          document.body.appendChild(paragraph);
          return [
            paragraph.getBoundingClientRect().height,
            normal.getBoundingClientRect().height,
            bold.getBoundingClientRect().height,
            bold.getBoundingClientRect().width > 200
          ];
        })()
    )JS", "native-adjacent-inline-runs.js");
    if (result != "[42,21,42,true]") {
        fail("adjacent inline runs did not share a two-line formatting context: " + result);
    }
}

void test_default_line_breaking_does_not_split_unbreakable_tokens(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const timeline = document.createElement('div');
          timeline.style.cssText =
            'position:relative;width:120px;height:32px;'
            + 'font-family:sans-serif;font-size:11px;line-height:14px';
          const track = document.createElement('div');
          track.style.cssText =
            'position:absolute;left:0;top:4px;width:120px;height:7px';
          const marker = document.createElement('i');
          marker.style.cssText =
            'position:absolute;left:70%;top:50%;width:1px;height:14px;'
            + 'transform:translate(-50%,-50%)';
          const label = document.createElement('div');
          label.style.cssText =
            'position:absolute;left:70%;top:14px;width:28px;'
            + 'transform:translateX(-50%)';
          label.textContent = '04:00';
          track.appendChild(marker);
          timeline.append(track, label);
          document.body.appendChild(timeline);
          const trackRect = track.getBoundingClientRect();
          const markerRect = marker.getBoundingClientRect();
          const labelRect = label.getBoundingClientRect();
          return {
            labelHeight: labelRect.height,
            labelTextHeight: label.firstChild.getBoundingClientRect().height,
            markerCenter: markerRect.top + markerRect.height / 2,
            trackCenter: trackRect.top + trackRect.height / 2
          };
        })()
    )JS", "native-unbreakable-timeline-label.js");
    if (result != R"({"labelHeight":14,"labelTextHeight":14,"markerCenter":7.5,"trackCenter":7.5})") {
        fail("default line breaking split a timeline token or displaced its marker: " + result);
    }
}

void test_inline_flex_preserves_padding_and_line_box(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .webscene-inline-flex-item-test { padding-right: 6px; }
            .webscene-inline-flex-title-test { padding-right: 1px; }
          `;
          document.body.appendChild(style);
          const wrapper = document.createElement('div');
          wrapper.style.alignItems = 'center';
          wrapper.style.display = 'flex';
          wrapper.style.height = '24px';
          const values = document.createElement('div');
          values.style.display = 'block';
          values.style.fontSize = '13px';
          values.style.lineHeight = '16px';
          values.style.whiteSpace = 'nowrap';
          const itemElements = [];
          const titleElements = [];
          const valueElements = [];
          for (const [title, value] of [['O', '197.43'], ['H', '198.81']]) {
            const item = document.createElement('span');
            item.className = 'webscene-inline-flex-item-test';
            item.style.alignItems = 'center';
            item.style.display = 'inline-flex';
            const titleElement = document.createElement('span');
            titleElement.className = 'webscene-inline-flex-title-test';
            titleElement.style.display = 'inline-flex';
            titleElement.textContent = title;
            const valueElement = document.createElement('span');
            valueElement.style.display = 'inline-flex';
            valueElement.textContent = value;
            item.appendChild(titleElement);
            item.appendChild(valueElement);
            values.appendChild(item);
            itemElements.push(item);
            titleElements.push(titleElement);
            valueElements.push(valueElement);
          }
          wrapper.appendChild(values);
          document.body.appendChild(wrapper);
          const wrapperRect = wrapper.getBoundingClientRect();
          const valuesRect = values.getBoundingClientRect();
          const items = itemElements.map(node => node.getBoundingClientRect());
          const titles = titleElements.map(node => node.getBoundingClientRect());
          const valueRects = valueElements.map(node => node.getBoundingClientRect());
          return {
            centered: Math.abs(valuesRect.y - (wrapperRect.y + 4)) < 0.01,
            lineBoxHeight: Math.round(valuesRect.height),
            itemHeights: items.map(rect => Math.round(rect.height)),
            firstTrailingPadding: Math.round(
              items[0].width - titles[0].width - valueRects[0].width),
            firstTitlePadding: Math.round(
              valueRects[0].x - titles[0].x - (titles[0].width - 1)),
            adjacent: Math.abs(items[1].x - items[0].right) < 0.01
          };
        })()
    )JS", "native-inline-flex-line-box.js");
    if (result != R"JSON({"centered":true,"lineBoxHeight":16,"itemHeights":[16,16],"firstTrailingPadding":6,"firstTitlePadding":1,"adjacent":true})JSON") {
        fail("inline-flex boxes lost their line height or padding: " + result);
    }
}
