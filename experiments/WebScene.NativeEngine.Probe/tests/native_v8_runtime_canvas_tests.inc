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
          scaled.addPath(source, new DOMMatrix().scaleSelf(0.5, 0.25));
          context.fillStyle = '#1e53e5';
          context.fill(scaled);
        })()
    )JS", "native-canvas-path2d-add-path.js");
    webscene_engine_request_scene_checkpoint(engine);
    bool observed_svg_fill = false;
    bool observed_stale_fill = false;
    bool observed_transform = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->canvas_command_count; ++index) {
                const auto& command = scene->canvas_commands[index];
                if (command.kind == 21U) observed_stale_fill = true;
                if (command.kind == 28U) {
                    observed_svg_fill = true;
                    observed_transform = (command.flags & 0xFFFFU) == 6U
                        && std::abs(command.data.values[0] - 0.5) < 0.001
                        && std::abs(command.data.values[3] - 0.25) < 0.001;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_svg_fill) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!observed_svg_fill || !observed_transform || observed_stale_fill) {
        fail("Path2D.addPath fill did not preserve its transform independently of the current path: svg="
            + std::to_string(observed_svg_fill) + ", transform="
            + std::to_string(observed_transform) + ", stale="
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
          return { dash, retained };
        })()
    )JS", "native-canvas-line-dash-path2d-arc.js");
    require(
        result == R"({"dash":[2,3,4,2,3,4],"retained":[2,3,4,2,3,4]})",
        "Canvas2D line-dash normalization or invalid-value handling regressed: " + result);

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
            + '`<svg><path fill="currentColor" fill-rule="evenodd" d="M0 0h10v10H0z"/></svg>`,'
            + '`image/svg+xml`);'
            + 'globalThis.__parsedFillRule = parsed.querySelector(`path`).getAttribute(`fill-rule`);'
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
