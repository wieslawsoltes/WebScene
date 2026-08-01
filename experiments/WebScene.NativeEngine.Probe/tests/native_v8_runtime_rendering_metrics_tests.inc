void test_generated_pseudo_element_opacity(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .status-halo {
              position: relative;
              width: 18px;
              height: 18px;
              --status-background: rgb(83, 186, 176);
            }
            .status-halo::after {
              content: '';
              position: absolute;
              inset: 0;
              width: 18px;
              height: 18px;
              background: var(--status-background);
              opacity: 0.25;
            }
          `;
          document.body.appendChild(style);
          const status = document.createElement('div');
          status.className = 'status-halo';
          document.body.appendChild(status);
        })()
    )JS", "native-pseudo-opacity-setup.js");
    require(
        evaluate(engine, "document.querySelector('.status-halo').offsetWidth",
            "native-pseudo-opacity-ready.js") == "18",
        "pseudo-opacity fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    uint32_t observed_alpha = 0;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if ((command.kind == 1U || command.kind == 7U)
                    && (command.rgba >> 8U) == 0x53BAB0U
                    && command.width == 18.0F
                    && command.height == 18.0F) {
                    observed_alpha = command.rgba & 0xFFU;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_alpha != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (observed_alpha < 63U || observed_alpha > 64U) {
        fail("generated pseudo-element opacity was not applied to scene alpha: "
            + std::to_string(observed_alpha));
    }
}

void test_negative_z_after_paints_behind_svg_content(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .negative-after-button {
              display: flex;
              position: relative;
              z-index: 2;
              width: 28px;
              height: 22px;
            }
            .negative-after-button::after {
              content: '';
              position: absolute;
              inset: 1px 0;
              z-index: -1;
              background: #0d1f22;
            }
            .negative-after-button svg {
              width: 18px;
              height: 18px;
              color: #9dabae;
            }
          `;
          document.body.appendChild(style);
          const button = document.createElement('button');
          button.className = 'negative-after-button';
          button.innerHTML = '<svg viewBox="0 0 18 18"><path fill="currentColor" d="M1 1h16v16H1z"/></svg>';
          document.body.appendChild(button);
        })()
    )JS", "native-negative-after-svg-setup.js");
    require(
        evaluate(engine, "document.querySelector('.negative-after-button').offsetWidth",
            "native-negative-after-svg-ready.js") == "28",
        "negative-z pseudo fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto pseudo_index = -1;
    auto svg_index = -1;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if ((command.kind == 9U || command.kind == 10U)
                    && (command.rgba >> 8U) == 0x0D1F22U) {
                    pseudo_index = static_cast<int>(index);
                }
                if (command.kind == 6U && (command.rgba >> 8U) == 0x9DABAEU) {
                    svg_index = static_cast<int>(index);
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (pseudo_index >= 0 && svg_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (pseudo_index < 0 || svg_index < 0 || pseudo_index >= svg_index) {
        fail("negative-z ::after did not paint behind its SVG content: pseudo="
            + std::to_string(pseudo_index) + ", svg=" + std::to_string(svg_index));
    }
}

void test_svg_current_color_is_resolved_before_scene_serialization(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const row = document.createElement('div');
          row.style.color = 'rgb(157, 171, 174)';
          row.style.width = '24px';
          row.style.height = '24px';
          row.innerHTML = '<svg viewBox="0 0 24 24" width="24" height="24">'
            + '<path fill="currentColor" d="M7 4l9 8-9 8z"/></svg>';
          document.body.appendChild(row);
        })()
    )JS", "native-svg-currentcolor-setup.js");
    require(
        evaluate(engine,
            "document.querySelector('svg').offsetWidth",
            "native-svg-currentcolor-ready.js") == "24",
        "SVG currentColor fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto found_resolved_svg = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 6U || command.flags >= scene->string_count) continue;
                const auto resource = scene->strings[command.flags];
                const std::string_view bytes(
                    scene->string_bytes + resource.byte_offset,
                    resource.byte_length);
                found_resolved_svg = bytes.find("fill=\"#9dabae\"") != std::string_view::npos
                    && bytes.find("color=\"#9dabae\"") != std::string_view::npos
                    && bytes.find("currentColor") == std::string_view::npos;
                if (found_resolved_svg) break;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found_resolved_svg) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(found_resolved_svg,
        "inherited SVG currentColor did not resolve to an explicit immutable-scene color");
}

void test_positive_z_before_paints_above_lower_z_child(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            #pseudo-stack-host {
              background: red;
              height: 120px;
              position: relative;
              width: 180px;
            }
            #pseudo-stack-host::before {
              background: rgb(0, 192, 0);
              border-radius: 12px;
              content: '';
              height: 50px;
              left: 30px;
              position: absolute;
              top: 20px;
              width: 90px;
              z-index: 2;
            }
            #pseudo-stack-child {
              background: rgb(0, 0, 192);
              height: 50px;
              left: 40px;
              position: absolute;
              top: 30px;
              width: 90px;
              z-index: 1;
            }
          `;
          document.body.appendChild(style);
          const host = document.createElement('div');
          host.id = 'pseudo-stack-host';
          host.innerHTML = '<div id="pseudo-stack-child"></div>';
          document.body.appendChild(host);
        })()
    )JS", "native-positive-before-stack-setup.js");
    require(
        evaluate(engine, "document.querySelector('#pseudo-stack-host').offsetWidth",
            "native-positive-before-stack-ready.js") == "180",
        "positive-z pseudo stacking fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto pseudo_index = -1;
    auto child_index = -1;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 10U
                    && (command.rgba >> 8U) == 0x00C000U) {
                    pseudo_index = static_cast<int>(index);
                }
                if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0x0000C0U) {
                    child_index = static_cast<int>(index);
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (pseudo_index >= 0 && child_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (pseudo_index < 0 || child_index < 0 || pseudo_index <= child_index) {
        fail("positive-z ::before did not paint above its lower-z child: pseudo="
            + std::to_string(pseudo_index) + ", child=" + std::to_string(child_index));
    }
}

void test_element_opacity_emits_isolated_group(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            #opacity-group {
              background: rgb(255, 0, 0);
              height: 90px;
              opacity: 0.5;
              position: relative;
              width: 140px;
            }
            #opacity-child {
              background: rgb(0, 0, 255);
              height: 50px;
              left: 30px;
              position: absolute;
              top: 20px;
              width: 80px;
            }
          `;
          document.body.appendChild(style);
          const group = document.createElement('div');
          group.id = 'opacity-group';
          const child = document.createElement('div');
          child.id = 'opacity-child';
          group.appendChild(child);
          document.body.appendChild(group);
        })()
    )JS", "native-opacity-group-setup.js");
    require(
        evaluate(engine, "document.querySelector('#opacity-group').offsetWidth",
            "native-opacity-group-ready.js") == "140",
        "opacity group fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto begin_index = -1;
    auto parent_index = -1;
    auto child_index = -1;
    auto end_index = -1;
    auto parent_alpha = 0U;
    auto child_alpha = 0U;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 30U && command.node_id != 0U) {
                    begin_index = static_cast<int>(index);
                    require((command.rgba & 0xFFU) == 128U,
                        "opacity group did not encode the expected half alpha");
                } else if (command.kind == 31U && begin_index >= 0) {
                    end_index = static_cast<int>(index);
                } else if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0xFF0000U) {
                    parent_index = static_cast<int>(index);
                    parent_alpha = command.rgba & 0xFFU;
                } else if ((command.kind == 1U || command.kind == 9U)
                    && (command.rgba >> 8U) == 0x0000FFU) {
                    child_index = static_cast<int>(index);
                    child_alpha = command.rgba & 0xFFU;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (begin_index >= 0 && parent_index >= 0 && child_index >= 0 && end_index >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        begin_index >= 0 && begin_index < parent_index
        && parent_index < child_index && child_index < end_index,
        "element opacity did not bracket the completed subtree as one scene group");
    require(parent_alpha == 255U && child_alpha == 255U,
        "element opacity was still multiplied into individual subtree paints");
}

void test_svg_background_image_reaches_scene_with_position_and_size(webscene_engine* engine)
{
    const auto resource_directory = std::filesystem::temp_directory_path()
        / ("webscene-css-background-svg-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(resource_directory);
    const auto svg_path = resource_directory / "badge.svg";
    {
        std::ofstream svg(svg_path, std::ios::binary);
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2 1"><rect width="2" height="1" fill="#00ff00"/></svg>)";
    }
    const auto root = resource_directory.string();
    require(
        webscene_engine_set_resource_root(engine, root.data(), root.size()) != 0,
        "CSS background fixture resource root was rejected");
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .svg-background {
              width: 100px;
              height: 60px;
              background-image: url("badge.svg");
              background-repeat: no-repeat;
              background-position: 0 2px;
              background-size: 48px auto;
            }
          `;
          document.body.appendChild(style);
          const target = document.createElement('div');
          target.className = 'svg-background';
          document.body.appendChild(target);
        })()
    )JS", "native-svg-background-setup.js");
    require(
        evaluate(engine, R"JS((() => {
          const style = getComputedStyle(document.querySelector('.svg-background'));
          return {
            image: style.backgroundImage,
            repeat: style.backgroundRepeat,
            position: style.backgroundPosition,
            size: style.backgroundSize
          };
        })())JS", "native-svg-background-cssom.js")
            .find(R"("repeat":"no-repeat")") != std::string::npos,
        "CSS background longhands were not exposed through computed style");

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.x) < 0.01F
                    && std::abs(command.y - 2.0F) < 0.01F
                    && std::abs(command.width - 48.0F) < 0.01F
                    && std::abs(command.height - 24.0F) < 0.01F
                    && command.flags < scene->string_count) {
                    const auto resource = scene->strings[command.flags];
                    const std::string_view bytes(
                        scene->string_bytes + resource.byte_offset,
                        resource.byte_length);
                    found = bytes.find("<svg") != std::string_view::npos
                        && bytes.find("0 0 2 1") != std::string_view::npos;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(resource_directory, cleanup_error);
    require(found, "URL-backed SVG background did not reach the scene at 0,2 48x24");
}

void test_svg_img_element_loads_and_reaches_scene(webscene_engine* engine)
{
    const auto resource_directory = std::filesystem::temp_directory_path()
        / ("webscene-img-svg-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(resource_directory);
    const auto svg_path = resource_directory / "symbol.svg";
    {
        std::ofstream svg(svg_path, std::ios::binary);
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 12"><rect width="24" height="12" fill="#2962ff"/></svg>)";
    }
    const auto root = resource_directory.string();
    require(
        webscene_engine_set_resource_root(engine, root.data(), root.size()) != 0,
        "IMG SVG fixture resource root was rejected");
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const image = document.createElement('img');
          image.id = 'native-svg-image';
          image.style.width = '18px';
          image.style.height = '18px';
          globalThis.__nativeSvgImageLoads = 0;
          image.onload = () => __nativeSvgImageLoads++;
          image.src = 'symbol.svg';
          document.body.appendChild(image);
        })()
    )JS", "native-svg-img-setup.js");

    std::string image_state;
    for (auto attempt = 0; attempt < 200; ++attempt) {
        image_state = evaluate(engine, R"JS((() => {
          const image = document.querySelector('#native-svg-image');
          return {
            complete: image.complete,
            currentSrc: image.currentSrc,
            naturalWidth: image.naturalWidth,
            naturalHeight: image.naturalHeight,
            loads: __nativeSvgImageLoads
          };
        })())JS", "native-svg-img-state.js");
        if (image_state.find(R"("complete":true)") != std::string::npos
            && image_state.find(R"("loads":1)") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(
        image_state.find(R"("currentSrc":)") != std::string::npos
            && image_state.find("symbol.svg") != std::string::npos
            && image_state.find(R"("naturalWidth":24)") != std::string::npos
            && image_state.find(R"("naturalHeight":12)") != std::string::npos
            && image_state.find(R"("loads":1)") != std::string::npos,
        "SVG IMG element did not expose an asynchronous loaded state: "
            + image_state);

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.x) < 0.01F
                    && std::abs(command.y) < 0.01F
                    && std::abs(command.width - 18.0F) < 0.01F
                    && std::abs(command.height - 18.0F) < 0.01F
                    && command.flags < scene->string_count) {
                    const auto resource = scene->strings[command.flags];
                    const std::string_view bytes(
                        scene->string_bytes + resource.byte_offset,
                        resource.byte_length);
                    found = bytes.find("<svg") != std::string_view::npos
                        && bytes.find("0 0 24 12") != std::string_view::npos;
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(resource_directory, cleanup_error);
    require(found, "loaded SVG IMG element did not reach the native scene");
}

void test_virtual_html_root_inherits_font_metrics(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = 'html { font-size: 10px; line-height: 1; }';
          document.body.appendChild(style);
          const row = document.createElement('div');
          row.innerHTML = '<span id="root-font-probe">x</span>';
          document.body.appendChild(row);
        })()
    )JS", "native-virtual-html-font-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const body = getComputedStyle(document.body);
      const probe = getComputedStyle(document.getElementById('root-font-probe'));
      return {
        bodyFont: body.fontSize,
        bodyLine: body.lineHeight,
        probeFont: probe.fontSize,
        probeLine: probe.lineHeight
      };
    })())JS", "native-virtual-html-font-read.js");
    require(
        state.find(R"("bodyFont":"10px")") != std::string::npos
            && state.find(R"("bodyLine":"10px")") != std::string::npos
            && state.find(R"("probeFont":"10px")") != std::string::npos
            && state.find(R"("probeLine":"10px")") != std::string::npos,
        "virtual HTML root font metrics did not inherit: " + state);
}

void test_font_shorthand_inherit_resets_control_metrics(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .font-parent {
              font-size: 20px;
              line-height: 30px;
              font-weight: 700;
              font-family: WebSceneFontProbe;
            }
            .font-child { font: inherit; }
          `;
          document.body.appendChild(style);
          const parent = document.createElement('div');
          parent.className = 'font-parent';
          const child = document.createElement('button');
          child.id = 'font-inherit-probe';
          child.className = 'font-child';
          child.textContent = 'Inherited control';
          parent.appendChild(child);
          document.body.appendChild(parent);
        })()
    )JS", "native-font-inherit-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.getElementById('font-inherit-probe'));
      return {
        size: style.fontSize,
        line: style.lineHeight,
        weight: style.fontWeight,
        family: style.fontFamily
      };
    })())JS", "native-font-inherit-read.js");
    require(
        state.find(R"("size":"20px")") != std::string::npos
            && state.find(R"("line":"30px")") != std::string::npos
            && state.find(R"("weight":"700")") != std::string::npos
            && state.find("WebSceneFontProbe") != std::string::npos,
        "font:inherit did not reset control metrics to inherited values: " + state);
}

void test_all_unset_resets_modeled_control_properties(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .all-parent {
              color: rgb(10, 20, 30);
              font-size: 19px;
              line-height: 27px;
            }
            .all-reset {
              width: 200px;
              height: 60px;
              padding: 11px;
              margin: 13px;
              border: 5px solid red;
              background: red;
              opacity: 0.25;
              transform: translate(12px, 8px);
              --all-token: retained;
              all: unset;
              display: flex;
              width: 90px;
            }
          `;
          document.body.appendChild(style);
          const parent = document.createElement('div');
          parent.className = 'all-parent';
          const control = document.createElement('button');
          control.id = 'all-unset-probe';
          control.className = 'all-reset';
          control.style.height = '44px';
          control.textContent = 'Reset control';
          parent.appendChild(control);
          document.body.appendChild(parent);
        })()
    )JS", "native-all-unset-setup.js");
    const auto state = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.getElementById('all-unset-probe'));
      return {
        display: style.display,
        width: style.width,
        height: style.height,
        padding: style.getPropertyValue('padding-left'),
        margin: style.getPropertyValue('margin-left'),
        border: style.borderTopWidth,
        background: style.backgroundColor,
        opacity: style.opacity,
        transform: style.transform,
        color: style.color,
        size: style.fontSize,
        line: style.lineHeight,
        token: style.getPropertyValue('--all-token')
      };
    })())JS", "native-all-unset-read.js");
    require(
        state.find(R"("display":"flex")") != std::string::npos
            && state.find(R"("width":"90px")") != std::string::npos
            && state.find(R"("height":"44px")") != std::string::npos
            && state.find(R"("padding":"0px")") != std::string::npos
            && state.find(R"("margin":"0px")") != std::string::npos
            && state.find(R"("border":"0px")") != std::string::npos
            && state.find("\"background\":\"rgba(0, 0, 0, 0)\"") != std::string::npos
            && state.find(R"("opacity":"1")") != std::string::npos
            && state.find(R"("transform":"none")") != std::string::npos
            && state.find("\"color\":\"rgb(10, 20, 30)\"") != std::string::npos
            && state.find(R"("size":"19px")") != std::string::npos
            && state.find(R"("line":"27px")") != std::string::npos
            && state.find(R"("token":"retained")") != std::string::npos,
        "all:unset did not reset modeled properties while preserving inline/custom state: "
            + state);

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"property:all","classification":"partially-supported","count":1,"semanticSlice":"non-important unset across modeled properties, excluding custom properties")")
            != std::string::npos,
        "all:unset was not classified at its native decision point: " + report);
#endif
}

void test_unsupported_features_are_reported_at_native_decision_points(webscene_engine* engine)
{
#if !defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("telemetryEnabled":false)") != std::string::npos
            && report.find("telemetry-disabled") != std::string::npos,
        "production feature-use API did not report compile-time telemetry exclusion");
    const auto scene_report = diagnostics(engine);
    require(
        scene_report.find("telemetry disabled at compile time") != std::string::npos,
        "production diagnostics API did not report compile-time telemetry exclusion");
#else
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            .unsupported-probe { backdrop-filter: blur(2px); }
            .alias-probe {
              display: flex;
              -moz-transform: translate(12px, 3px);
              transform-origin: left center;
              grid-gap: 7px;
            }
            .clip-composition { overflow: hidden; }
            .shadow-composition {
              border-radius: 6px;
              box-shadow: 0 2px 6px rgba(0, 0, 0, .5);
            }
            .relative-unit-probe { width: 12em; height: 1rem; margin-left: 1vw; }
            .quoted-unit-probe::before { content: "99q"; }
            @media screen and (min-width: 10px) {
              .media-probe { width: calc(8px + 2px); }
            }
            @media (prefers-color-scheme: dark) {
              .unsupported-media-probe { width: 1px; }
            }
            @supports selector(:focus-visible) {
              .supported-query-probe:focus-visible { opacity: 1; }
            }
            @supports (display: grid) {
              .unsupported-query-probe { display: grid; }
            }
            @layer components {
              .layer-probe { color: rgb(1, 2, 3); }
            }
            @container size (width > 10px) {
              .container-probe { color: hsl(0, 0%, 0%); }
            }
            .selector-probe:is(.one, .two):nth-child(2) > [data-probe^="value"]::marker {
              transform: translateX(2px) scale(1.1);
            }
          `;
          document.body.appendChild(style);
          const target = document.createElement('div');
          target.className = 'unsupported-probe';
          target.addEventListener('click', () => {});
          target.addEventListener('webscene-never-dispatched', () => {});
          target.addEventListener('webscene-passive', () => {}, { passive: true });
          target.addEventListener('webscene-dispatched', () => {});
          target.dispatchEvent(new Event('webscene-dispatched'));
          target.setAttribute('aria-label', 'feature probe');
          target.setAttribute('draggable', 'true');
          document.body.appendChild(target);
          const alias = document.createElement('div');
          alias.className = 'alias-probe';
          document.body.appendChild(alias);
          const clip = document.createElement('div');
          clip.className = 'clip-composition';
          const shadow = document.createElement('div');
          shadow.className = 'shadow-composition';
          clip.appendChild(shadow);
          document.body.appendChild(clip);
          const canvas = document.createElement('canvas');
          document.body.appendChild(canvas);
          canvas.getContext('2d').ellipse(10, 10, 5, 5, 0, 0, Math.PI * 2);
          const input = document.createElement('input');
          input.setAttribute('autocomplete', 'off');
          input.setAttribute('placeholder', 'feature probe');
          document.body.appendChild(input);
          sessionStorage.setItem('feature-probe', 'value');
          sessionStorage.getItem('feature-probe');
          sessionStorage.removeItem('feature-probe');
          new Blob(['feature-probe']);
          new URL('/feature-probe', location.href);
          crypto.getRandomValues(new Uint8Array(4));
          matchMedia('(min-width: 1px)');
          performance.now();
          performance.mark('feature-probe');
          performance.getEntriesByName('feature-probe');
          void (typeof localStorage);
        })()
    )JS", "native-feature-use-probe.js");
    require(
        evaluate(engine, "document.querySelector('.unsupported-probe').offsetWidth >= 0",
            "native-feature-use-barrier.js") == "true",
        "feature-use fixture did not settle");

    const auto report = feature_use(engine);
    require(
        report.find(R"("schema":"webscene-native-feature-use-v2")") != std::string::npos
            && report.find(R"("complete":false)") != std::string::npos
            && report.find(R"("schema":"webscene-native-composition-use-v1")")
                != std::string::npos
            && report.find(R"("id":"css-transform-origin-independent-cascade")")
                != std::string::npos
            && report.find(R"({"id":"css-transform-origin-independent-cascade","count":)")
                != std::string::npos
            && report.find(R"({"id":"css-shadow-radius-overflow","count":)")
                != std::string::npos
            && report.find("canvas-supported-operations") == std::string::npos
            && report.find(R"("feature":"property:backdrop-filter")") != std::string::npos
            && report.find(R"("feature":"CanvasRenderingContext2D.ellipse")") != std::string::npos
            && report.find(R"("feature":"property:-moz-transform","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"property:grid-gap","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@media","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"media-feature:prefers-color-scheme","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@supports","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@supports","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@layer","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"at-rule:@container","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-class:nth-child","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-class:is","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:attribute-operator:^=","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"selector:pseudo-element:marker","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"function:calc","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:em","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:rem","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:vw","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"length-unit:q")") == std::string::npos
            && report.find(R"("feature":"attribute:aria-label","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:draggable","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:autocomplete","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"attribute:placeholder","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"function:hsl","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"method:EventTarget.addEventListener","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-never-dispatched","classification":"unobserved-code-path")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-dispatched","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"type:webscene-dispatched","classification":"unobserved-code-path")")
                == std::string::npos
            && report.find(R"("feature":"Storage.setItem","classification":"supported")")
                != std::string::npos
            && report.find(R"("feature":"Blob.constructor","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Crypto.getRandomValues","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Window.matchMedia","classification":"partially-supported")")
                != std::string::npos
            && report.find(R"("feature":"Performance.getEntriesByName","classification":"unsupported")")
                != std::string::npos
            && report.find(R"("feature":"global:localStorage","classification":"unsupported")")
                == std::string::npos
            && report.find(R"("classification":"unsupported")") != std::string::npos,
        "unsupported native decisions were absent from the structured report: " + report);

    const auto listeners = event_listener_inventory(engine);
    require(
        listeners.find(R"("schema":"webscene-event-listener-inventory-v1")") != std::string::npos
            && listeners.find(R"("complete":true)") != std::string::npos
            && listeners.find(R"("eventTypes":["click","webscene-dispatched")")
                != std::string::npos,
        "registered listener targets were absent from the structured inventory: " + listeners);
#endif
}

void test_engine_memory_metrics_are_worker_snapshots(webscene_engine* engine)
{
    struct original_memory_metrics final {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t values[10];
    };
    static_assert(
        sizeof(original_memory_metrics)
            == offsetof(webscene_engine_memory_metrics, v8_code_and_metadata_bytes));

    execute(
        engine,
        "globalThis.__websceneMemoryProbe = new Array(4096).fill('probe')",
        "memory-metrics.js");
    webscene_engine_memory_metrics undersized{};
    require(
        webscene_engine_get_memory_metrics(engine, &undersized) == 0,
        "memory metrics accepted an undersized ABI structure");
    original_memory_metrics original{sizeof(original_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(
            engine,
            reinterpret_cast<webscene_engine_memory_metrics*>(&original)) != 0,
        "memory metrics rejected the original ABI structure prefix");
    webscene_engine_memory_metrics previous_tail{};
    previous_tail.struct_size =
        offsetof(webscene_engine_memory_metrics, v8_young_space_used_bytes);
    previous_tail.native_event_listener_storage_bytes =
        (std::numeric_limits<uint64_t>::max)();
    require(
        webscene_engine_get_memory_metrics(engine, &previous_tail) != 0
            && previous_tail.native_event_listener_storage_bytes
                != (std::numeric_limits<uint64_t>::max)(),
        "memory metrics rejected or truncated the previous additive ABI tail");

    webscene_engine_memory_metrics metrics{
        sizeof(webscene_engine_memory_metrics)};
    metrics.v8_code_and_metadata_bytes =
        (std::numeric_limits<uint64_t>::max)();
    metrics.v8_bytecode_and_metadata_bytes =
        (std::numeric_limits<uint64_t>::max)();
    for (auto attempt = 0; attempt < 100 && metrics.v8_total_heap_bytes == 0; ++attempt) {
        require(
            webscene_engine_get_memory_metrics(engine, &metrics) != 0,
            "memory metrics ABI was unavailable");
        if (metrics.v8_total_heap_bytes == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        metrics.v8_total_heap_bytes > 0
            && metrics.v8_used_heap_bytes > 0
            && metrics.v8_physical_heap_bytes > 0,
        "V8 heap memory was absent from the worker snapshot");
    require(
        metrics.v8_used_heap_bytes <= metrics.v8_total_heap_bytes,
        "V8 used heap exceeded total heap");
    require(
        metrics.v8_code_and_metadata_bytes
                != (std::numeric_limits<uint64_t>::max)()
            && metrics.v8_bytecode_and_metadata_bytes
                != (std::numeric_limits<uint64_t>::max)(),
        "V8 code/bytecode memory fields were absent from the worker snapshot");
    const auto attributed_space_used =
        metrics.v8_young_space_used_bytes
        + metrics.v8_old_space_used_bytes
        + metrics.v8_code_space_used_bytes
        + metrics.v8_map_space_used_bytes
        + metrics.v8_large_object_space_used_bytes
        + metrics.v8_read_only_space_used_bytes
        + metrics.v8_shared_space_used_bytes
        + metrics.v8_trusted_space_used_bytes;
    const auto attributed_space_physical =
        metrics.v8_young_space_physical_bytes
        + metrics.v8_old_space_physical_bytes
        + metrics.v8_code_space_physical_bytes
        + metrics.v8_map_space_physical_bytes
        + metrics.v8_large_object_space_physical_bytes
        + metrics.v8_read_only_space_physical_bytes
        + metrics.v8_shared_space_physical_bytes
        + metrics.v8_trusted_space_physical_bytes;
    require(
        attributed_space_used > 0
            && attributed_space_physical > 0
            && attributed_space_used
                <= metrics.v8_used_heap_bytes
                    + metrics.v8_read_only_space_used_bytes
            && attributed_space_physical
                <= metrics.v8_physical_heap_bytes
                    + metrics.v8_read_only_space_physical_bytes,
        "V8 heap-space attribution was absent or exceeded aggregate heap metrics");
    require(
        metrics.native_dom_node_count > 0
            && metrics.native_dom_node_size_bytes == sizeof(webscene_native::dom_node)
            && metrics.native_dom_inline_bytes
                == metrics.native_dom_node_count * metrics.native_dom_node_size_bytes,
        "native DOM allocation attribution was absent or internally inconsistent");
    require(
        metrics.native_dom_node_size_bytes <= 1000,
        "cold pseudo-element, canvas, animation, custom-property, background, grid, table, or form state regressed into every native DOM node");
    require(
        metrics.native_dom_textual_style_count <= metrics.native_dom_node_count
            && (metrics.native_dom_textual_style_count == 0
                || metrics.native_dom_textual_style_storage_bytes
                    >= metrics.native_dom_textual_style_count
                        * sizeof(webscene_native::node_style::textual_style_data)),
        "sparse textual-style allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_table_layout_count <= metrics.native_dom_node_count
            && (metrics.native_dom_table_layout_count == 0
                || metrics.native_dom_table_layout_storage_bytes
                    >= metrics.native_dom_table_layout_count
                        * sizeof(webscene_native::dom_node::table_layout_data))
            && metrics.native_dom_form_control_count <= metrics.native_dom_node_count
            && (metrics.native_dom_form_control_count == 0
                || metrics.native_dom_form_control_storage_bytes
                    >= metrics.native_dom_form_control_count
                        * sizeof(webscene_native::dom_node::form_control_data)),
        "sparse table/form allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_attribute_node_count <= metrics.native_dom_node_count
            && (metrics.native_dom_attribute_entry_count == 0
                || metrics.native_dom_attribute_storage_bytes
                    >= metrics.native_dom_attribute_entry_count
                        * sizeof(webscene_native::attribute_collection::value_type)),
        "native compact-attribute allocation attribution was internally inconsistent");
    require(
        metrics.native_dom_animation_count <= metrics.native_dom_node_count
            && (metrics.native_dom_animation_count == 0
                || metrics.native_dom_animation_storage_bytes
                    >= metrics.native_dom_animation_count
                        * sizeof(webscene_native::node_style::animation_data)),
        "native animation allocation attribution was internally inconsistent");
}

void test_hidden_engine_reclamation_is_debounced_and_cancelable(
    webscene_engine* engine)
{
    const auto read_hidden_notifications = [&]() {
        webscene_engine_memory_metrics metrics{sizeof(webscene_engine_memory_metrics)};
        require(
            webscene_engine_get_memory_metrics(engine, &metrics) != 0,
            "hidden-memory policy could not read engine metrics");
        return metrics.hidden_low_memory_notifications;
    };

    require(
        webscene_engine_set_visible(engine, 0) != 0,
        "engine rejected hidden host state");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(
        webscene_engine_set_visible(engine, 1) != 0,
        "engine rejected restored visible host state");
    const auto before_cancel_window = read_hidden_notifications();
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    require(
        read_hidden_notifications() == before_cancel_window,
        "transient hidden state was not canceled before reclamation");

    require(
        webscene_engine_set_visible(engine, 0) != 0,
        "engine rejected sustained hidden host state");
    auto observed = false;
    for (auto attempt = 0; attempt < 200; ++attempt) {
        if (read_hidden_notifications() > before_cancel_window) {
            observed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(
        observed,
        "sustained hidden state did not schedule worker-thread reclamation");
    require(
        webscene_engine_set_visible(engine, 1) != 0,
        "engine rejected final visible host state");
}
