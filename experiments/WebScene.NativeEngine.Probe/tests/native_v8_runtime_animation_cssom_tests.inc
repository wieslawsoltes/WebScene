void test_opacity_and_color_transitions_use_host_clock_and_dispatch_events(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #temporal-paint {
                position: fixed; left: 10px; top: 10px; width: 40px; height: 20px;
                opacity: .2; color: rgb(10, 20, 30);
                transition-property: opacity, color;
                transition-duration: 100ms, 200ms;
                transition-delay: 20ms, 0ms;
                transition-timing-function: linear, linear;
              }
              #temporal-paint.selected { opacity: 1; color: rgb(110, 120, 130); }
              #temporal-paint.reversed { opacity: .2; color: rgb(10, 20, 30); }
            </style>
            <div id="temporal-paint">paint</div>`;
          const target = document.getElementById('temporal-paint');
          globalThis.__transitionEvents = [];
          for (const type of ['transitionrun', 'transitionstart', 'transitionend', 'transitioncancel']) {
            target.addEventListener(type, event => {
              globalThis.__transitionEvents.push(`${type}:${event.propertyName}`);
            });
          }
        })()
    )JS", "paint-transition-host-clock-setup.js");
    animation_frame_and_wait(engine, 0.0, 7011U);
    execute_and_wait(engine,
        "document.getElementById('temporal-paint').classList.add('selected')",
        "paint-transition-activate.js");

    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return [style.transitionProperty, style.transitionDuration,
            style.transitionDelay, style.transitionTimingFunction];
        })()
    )JS", "paint-transition-metadata.js");
    require(
        metadata == R"(["opacity, color","100ms, 200ms","20ms, 0ms","linear, linear"])" ,
        "computed transition metadata was not observable: " + metadata);

    animation_frame_and_wait(engine, 50.0, 7012U);
    const auto midpoint = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return {
            opacity: Math.round(Number(style.opacity) * 1000) / 1000,
            color: style.color,
            events: globalThis.__transitionEvents
          };
        })()
    )JS", "paint-transition-midpoint.js");
    require(
        midpoint.find(R"("opacity":0.44)") != std::string::npos
            && midpoint.find("\"color\":\"rgb(35, 45, 55)\"") != std::string::npos
            && midpoint.find("transitionrun:opacity") != std::string::npos
            && midpoint.find("transitionrun:color") != std::string::npos
            && midpoint.find("transitionstart:opacity") != std::string::npos
            && midpoint.find("transitionstart:color") != std::string::npos,
        "opacity/color midpoint or transition events were not host-clock driven: " + midpoint);

    // Reverse while both properties are running. The old transitions must be
    // cancelled and the new transitions must start from the painted midpoint.
    execute_and_wait(engine,
        "(() => { const target = document.getElementById('temporal-paint');"
        " target.style.setProperty('opacity', '.2');"
        " target.style.setProperty('color', 'rgb(10, 20, 30)'); })()",
        "paint-transition-reverse.js");
    require(
        evaluate(engine,
            "document.getElementById('temporal-paint').style.getPropertyValue('opacity')",
            "paint-transition-reverse-flush.js") == "\".2\"",
        "inline reversal mutation did not reach the DOM before the next host frame");
    animation_frame_and_wait(engine, 100.0, 7013U);
    const auto reversed = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return { opacity: Math.round(Number(style.opacity) * 1000) / 1000, color: style.color,
            events: globalThis.__transitionEvents };
        })()
    )JS", "paint-transition-reversed-midpoint.js");
    require(
        reversed.find("transitioncancel:opacity") != std::string::npos
            && reversed.find("transitioncancel:color") != std::string::npos
            && reversed.find(R"("opacity":0.368)") != std::string::npos
            && reversed.find("\"color\":\"rgb(29, 39, 49)\"") != std::string::npos,
        "transition cancellation/reversal did not retain the painted midpoint: " + reversed);

    animation_frame_and_wait(engine, 300.0, 7014U);
    const auto completed = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('temporal-paint'));
          return { opacity: Math.round(Number(style.opacity) * 1000) / 1000, color: style.color,
            events: globalThis.__transitionEvents };
        })()
    )JS", "paint-transition-complete.js");
    require(
        completed.find(R"("opacity":0.2)") != std::string::npos
            && completed.find("\"color\":\"rgb(10, 20, 30)\"") != std::string::npos
            && completed.find("transitionend:opacity") != std::string::npos
            && completed.find("transitionend:color") != std::string::npos,
        "opacity/color transitions did not publish their final state: " + completed);
}

void test_opacity_keyframes_use_host_clock_with_staggered_infinite_delays(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @keyframes dot-pulse {
                0%, to { opacity: 1; }
                60% { opacity: .4; }
                30% { opacity: .2; }
              }
              .dot { animation: dot-pulse 1000ms linear infinite; }
              #dot-a { animation-delay: 100ms; }
              #dot-b { animation-delay: 300ms; }
              #dot-c { animation-delay: 450ms; }
            </style>
            <div id="dot-host">
              <span id="dot-a" class="dot"></span>
              <span id="dot-b" class="dot"></span>
              <span id="dot-c" class="dot"></span>
            </div>`;
        })()
    )JS", "opacity-keyframes-host-clock-setup.js");

    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('dot-a'));
          return [style.animationName, style.animationDuration, style.animationDelay,
            style.animationTimingFunction, style.animationIterationCount];
        })()
    )JS", "opacity-keyframes-metadata.js");
    require(
        metadata == R"(["dot-pulse","1000ms","100ms","linear","infinite"])",
        "computed CSS animation metadata was not observable: " + metadata);
    wait_for_animation_frame_demand(
        engine,
        true,
        "visible infinite CSS animation did not request a host frame");

    animation_frame_and_wait(engine, 550.0, 7021U);
    const auto first = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-first-frame.js");
    require(first == "[0.6,1,1]",
        "staggered animation delays did not retain pre-start opacity: " + first);

    animation_frame_and_wait(engine, 900.0, 7022U);
    const auto second = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-second-frame.js");
    require(second == "[0.333,0.2,0.6]",
        "keyframe stops were not interpolated on the host clock: " + second);

    animation_frame_and_wait(engine, 1450.0, 7023U);
    const auto wrapped = evaluate(engine, R"JS(
        (() => [...document.querySelectorAll('.dot')].map(node =>
          Math.round(Number(getComputedStyle(node).opacity) * 1000) / 1000))()
    )JS", "opacity-keyframes-wrapped-frame.js");
    require(wrapped == "[0.867,0.775,0.55]",
        "infinite opacity animation did not wrap continuously: " + wrapped);

    execute_and_wait(
        engine,
        "document.getElementById('dot-host').style.display = 'none'",
        "opacity-keyframes-hide.js");
    wait_for_animation_frame_demand(
        engine,
        false,
        "display:none CSS animations retained host-frame demand");

    execute_and_wait(
        engine,
        "document.getElementById('dot-host').style.display = 'block'",
        "opacity-keyframes-show.js");
    wait_for_animation_frame_demand(
        engine,
        true,
        "restored CSS animations did not resume host-frame demand");

    execute_and_wait(
        engine,
        "document.body.innerHTML = '<div>static replacement</div>'",
        "opacity-keyframes-detach.js");
    wait_for_animation_frame_demand(
        engine,
        false,
        "detached CSS animations retained host-frame demand");
}

void test_rotation_keyframes_use_host_clock_and_wrap_continuously(
    webscene_engine* engine)
{
    execute_and_wait(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @keyframes spinner-rotation { to { transform: rotate(1turn); } }
              #spinner {
                box-sizing: border-box;
                width: 56px; height: 56px;
                border: 4px solid rgb(0, 188, 174);
                border-right-color: rgb(128, 128, 128);
                border-bottom-color: rgb(128, 128, 128);
                border-radius: 50%;
                animation: spinner-rotation 900ms linear infinite;
              }
            </style>
            <div id="spinner"></div>`;
        })()
    )JS", "rotation-keyframes-host-clock-setup.js");

    // Publish the insertion frame so the animation timeline starts at the
    // current host timestamp rather than at the first sampled intermediate frame.
    animation_frame_and_wait(engine, 1450.0, 7030U);
    const auto metadata = evaluate(engine, R"JS(
        (() => {
          const style = getComputedStyle(document.getElementById('spinner'));
          return [style.animationName, style.animationDuration,
            style.animationTimingFunction, style.animationIterationCount];
        })()
    )JS", "rotation-keyframes-metadata.js");
    require(
        metadata == R"(["spinner-rotation","900ms","linear","infinite"])" ,
        "computed rotate() animation metadata was not observable: " + metadata);

    webscene_engine_request_scene_checkpoint(engine);
    auto initial_scene_published = false;
    for (auto attempt = 0; attempt < 100 && !initial_scene_published; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            initial_scene_published =
                scene->header.consumed_input_sequence >= 7030U;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!initial_scene_published) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial_scene_published,
        "spinner insertion frame did not publish before temporal sampling");
    webscene_engine_metrics layout_before_animation{};
    webscene_engine_get_metrics(engine, &layout_before_animation);

    const auto angle_expression = R"JS(
        (() => {
          const value = getComputedStyle(document.getElementById('spinner'))
            .getPropertyValue('transform');
          const matrix = value.slice(7, -1).split(',').map(Number);
          let angle = Math.atan2(matrix[1], matrix[0]) * 180 / Math.PI;
          if (angle < 0) angle += 360;
          return Math.round(angle * 1000) / 1000;
        })()
    )JS";

    animation_frame_and_wait(engine, 1675.0, 7031U);
    const auto quarter_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-quarter-turn.js");
    require(
        std::abs(std::strtof(quarter_turn.c_str(), nullptr) - 90.0F) < 0.1F,
        "rotate() keyframes did not publish a quarter turn: " + quarter_turn);

    webscene_engine_request_scene_checkpoint(engine);
    auto painted_rotation = 0.0F;
    auto rounded_border_count = 0U;
    auto rounded_side_union = 0U;
    auto partitioned_border_count = 0U;
    constexpr uint32_t border_side_mask = 0xF0000000U;
    constexpr uint32_t border_color_partition = 1U << 27U;
    for (auto attempt = 0; attempt < 100 && rounded_border_count != 2U; ++attempt) {
        rounded_border_count = 0U;
        rounded_side_union = 0U;
        partitioned_border_count = 0U;
        painted_rotation = 0.0F;
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.consumed_input_sequence >= 7031U) {
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (command.kind == 19U) painted_rotation = command.stroke_width;
                    if (command.kind == 8U || command.kind == 11U) {
                        ++rounded_border_count;
                        if ((command.flags & border_color_partition) != 0U) {
                            ++partitioned_border_count;
                        }
                        rounded_side_union |= command.flags & border_side_mask;
                        require(
                            command.radius_top_left > 20.0F
                                && command.radius_top_right > 20.0F
                                && command.radius_bottom_right > 20.0F
                                && command.radius_bottom_left > 20.0F,
                            "spinner border-radius did not reach native paint commands");
                    }
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (rounded_border_count != 2U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        std::abs(painted_rotation - 90.0F) < 0.1F,
        "computed spinner rotation did not reach the native scene");
    require(
        rounded_border_count == 2U
            && partitioned_border_count == 2U
            && rounded_side_union == border_side_mask,
        "multi-colour rounded spinner border was flattened into square side rectangles");
    webscene_engine_metrics layout_after_animation{};
    webscene_engine_get_metrics(engine, &layout_after_animation);
    require(
        layout_after_animation.layout_passes
            == layout_before_animation.layout_passes,
        "paint-only rotation repeated full document layout");

    animation_frame_and_wait(engine, 1900.0, 7032U);
    const auto half_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-half-turn.js");
    require(
        std::abs(std::strtof(half_turn.c_str(), nullptr) - 180.0F) < 0.1F,
        "rotate() keyframes did not publish a half turn: " + half_turn);

    animation_frame_and_wait(engine, 2575.0, 7033U);
    const auto wrapped_quarter_turn = evaluate(
        engine, angle_expression, "rotation-keyframes-wrapped-quarter-turn.js");
    require(
        std::abs(std::strtof(wrapped_quarter_turn.c_str(), nullptr) - 90.0F) < 0.1F,
        "infinite rotate() keyframes did not wrap continuously: "
            + wrapped_quarter_turn);
}

void test_cssom_serializes_resolved_numbers_without_trailing_zeroes(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .canonical-number { font-size: 18px; line-height: 2em !important; }
            </style>
            <p id="canonical-number" class="canonical-number" style="line-height: 1em"></p>
            <div id="normal-line" style="font-size: 16px; line-height: normal"></div>
            <div id="percent-line" style="font-size: 16px; line-height: 10%"></div>`;
          document.getElementById('normal-line').style.left = '10px';
        })()
    )JS", "native-cssom-canonical-number-setup.js");
    const auto value = evaluate(engine,
        "getComputedStyle(document.getElementById('canonical-number')).lineHeight",
        "native-cssom-canonical-number-read.js");
    require(value == "\"36px\"",
        "resolved CSSOM numbers retained implementation trailing zeroes: " + value);
    const auto reflection = evaluate(engine, R"JS(
        (() => {
          const normal = document.getElementById('normal-line');
          const percent = document.getElementById('percent-line');
          return [
            normal.style.lineHeight,
            normal.style.left,
            getComputedStyle(normal).lineHeight,
            percent.style.lineHeight,
            getComputedStyle(percent).lineHeight
          ];
        })()
    )JS", "native-cssom-specified-and-resolved-read.js");
    require(reflection == R"(["normal","10px","normal","10%","1.6px"])",
        "specified and resolved CSSOM values were conflated: " + reflection);
}

void test_cssom_serializes_inline_hex_colors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const target = document.createElement('div');
          target.style.color = '#FFFFFF';
          target.style.backgroundColor = '#12345680';
          target.style.setProperty('--theme-color', '#FFFFFF');
          const color = target.style.color;
          const parserCompatible =
            (/^rgb\s*\(\s*(\d+),\s*(\d+),\s*(\d+)\s*\)$/).test(color) ||
            (/^rgba\s*\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+(?:\.\d+)?)\s*\)$/).test(color);
          const result = {
            color,
            colorByName: target.style.getPropertyValue('color'),
            background: target.style.backgroundColor,
            parserCompatible,
            cssText: target.style.cssText,
            attribute: target.getAttribute('style'),
            custom: target.style.getPropertyValue('--theme-color')
          };
          target.style.color = 'white';
          result.keyword = target.style.color;
          return result;
        })()
    )JS", "native-cssom-inline-hex-color.js");
    require(
        result == R"JSON({"color":"rgb(255, 255, 255)","colorByName":"rgb(255, 255, 255)","background":"rgba(18, 52, 86, 0.502)","parserCompatible":true,"cssText":"--theme-color: #FFFFFF; background-color: rgba(18, 52, 86, 0.502); color: rgb(255, 255, 255);","attribute":"--theme-color: #FFFFFF; background-color: rgba(18, 52, 86, 0.502); color: rgb(255, 255, 255);","custom":"#FFFFFF","keyword":"white"})JSON",
        "inline hex colors were not serialized using browser CSSOM form: " + result);
}

void test_cssom_padding_assignment_updates_longhands_and_geometry(webscene_engine* engine)
{
    const auto assigned = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="padding-probe" style="width:100px;height:20px"></div>';
          const probe = document.getElementById('padding-probe');
          probe.style.padding = '3px';
          probe.style.setProperty('padding-left', '7px');
          const style = getComputedStyle(probe);
          const rect = probe.getBoundingClientRect();
          return [
            probe.style.getPropertyValue('padding'),
            style.paddingTop,
            style.paddingRight,
            style.paddingBottom,
            style.paddingLeft,
            style.width,
            style.height,
            rect.width,
            rect.height
          ];
        })()
    )JS", "native-cssom-padding-assignment.js");
    require(assigned == R"JSON(["3px","3px","3px","3px","7px","100px","20px",110,26])JSON",
        "padding CSSOM assignment did not update declarations or geometry: " + assigned);

    const auto removed = evaluate(engine, R"JS(
        (() => {
          const probe = document.getElementById('padding-probe');
          probe.style.cssText = 'width:100px;height:20px;padding:3px';
          probe.style.padding = '';
          const style = getComputedStyle(probe);
          const rect = probe.getBoundingClientRect();
          return [
            style.paddingTop,
            style.paddingRight,
            style.paddingBottom,
            style.paddingLeft,
            rect.width,
            rect.height
          ];
        })()
    )JS", "native-cssom-padding-removal.js");
    require(removed == R"JSON(["0px","0px","0px","0px",100,20])JSON",
        "padding CSSOM removal left stale declarations or geometry: " + removed);
}

void test_cssom_border_assignment_updates_longhands_and_geometry(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="border-probe" style="width:100px;height:20px"></div>';
          const probe = document.getElementById('border-probe');
          probe.style.border = '3px';
          probe.style.borderStyle = 'solid';
          const first = getComputedStyle(probe);
          const firstRect = probe.getBoundingClientRect();
          const result = [
            probe.style.getPropertyValue('border'),
            first.borderTopWidth,
            first.borderRightWidth,
            first.borderBottomWidth,
            first.borderLeftWidth,
            first.width,
            first.height,
            firstRect.width,
            firstRect.height
          ];
          probe.style.borderWidth = '1px 2px 3px 4px';
          const expanded = getComputedStyle(probe);
          result.push(
            expanded.borderTopWidth,
            expanded.borderRightWidth,
            expanded.borderBottomWidth,
            expanded.borderLeftWidth
          );
          probe.style.borderTop = '5px solid red';
          const overridden = getComputedStyle(probe);
          const finalRect = probe.getBoundingClientRect();
          result.push(
            overridden.borderTopWidth,
            overridden.borderTopColor,
            finalRect.width,
            finalRect.height
          );
          return result;
        })()
    )JS", "native-cssom-border-assignment.js");
    require(
        value == R"JSON(["3px","3px","3px","3px","3px","100px","20px",106,26,"1px","2px","3px","4px","5px","rgb(255, 0, 0)",106,28])JSON",
        "border CSSOM assignment did not update longhands or geometry: " + value);
}

void test_logical_inline_borders_reach_geometry(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .widgetbar-pages {
                width:100px;
                height:20px;
                border-inline-end:1px solid;
                border-inline-start:1px solid;
                border-color:rgb(235,235,235);
              }
            </style>
            <div class="widgetbar-pages"></div>`;
          const pages = document.querySelector('.widgetbar-pages');
          const style = getComputedStyle(pages);
          const rect = pages.getBoundingClientRect();
          return [
            style.borderLeftWidth,
            style.borderRightWidth,
            style.borderLeftColor,
            style.borderRightColor,
            rect.width
          ];
        })()
    )JS", "native-logical-inline-borders.js");
    require(
        value == R"JSON(["1px","1px","rgb(235, 235, 235)","rgb(235, 235, 235)",102])JSON",
        "logical inline borders did not reach physical geometry: " + value);
}

void test_hidden_subtree_retains_computed_height_without_boxes(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML =
            '<div id="parent" style="display:none">'
            + '<input id="input" type="text" style="height:20px">'
            + '<textarea id="textarea" style="height:20px"></textarea>'
            + '<div id="block" style="height:20px"></div></div>';
          const parent = document.getElementById('parent');
          const input = document.getElementById('input');
          const textarea = document.getElementById('textarea');
          const block = document.getElementById('block');
          const hidden = [
            getComputedStyle(input).height,
            getComputedStyle(textarea).height,
            getComputedStyle(block).height,
            input.offsetHeight,
            textarea.offsetHeight,
            block.offsetHeight,
            input.getClientRects().length,
            textarea.getClientRects().length,
            block.getClientRects().length
          ];
          parent.style.display = 'block';
          hidden.push(
            getComputedStyle(input).height,
            getComputedStyle(textarea).height,
            getComputedStyle(block).height,
            input.getBoundingClientRect().height > 0,
            textarea.getBoundingClientRect().height > 0,
            block.getBoundingClientRect().height > 0
          );
          return hidden;
        })()
    )JS", "native-hidden-subtree-computed-height.js");
    require(
        value == R"JSON(["20px","20px","20px",0,0,0,0,0,0,"20px","20px","20px",true,true,true])JSON",
        "hidden subtree conflated computed dimensions with suppressed geometry: " + value);
}

void test_cssom_z_index_survives_connection_and_recascade(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<style>#target { z-index: 7; }</style>';
          const target = document.createElement('div');
          target.id = 'target';
          target.style.position = 'absolute';
          target.style.zIndex = 1000;
          document.body.appendChild(target);
          const result = [
            target.style.zIndex,
            target.style.getPropertyValue('z-index'),
            getComputedStyle(target).zIndex
          ];
          target.style.setProperty('z-index', '-2');
          result.push(target.style.zIndex, getComputedStyle(target).zIndex);
          target.style.removeProperty('z-index');
          result.push(target.style.zIndex, getComputedStyle(target).zIndex);
          return result;
        })()
    )JS", "native-cssom-z-index-connection.js");
    require(
        value == R"JSON(["1000","1000","1000","-2","-2","","7"])JSON",
        "z-index CSSOM assignment was lost during connection or recascade: " + value);
}

void test_important_custom_property_cascade_reaches_paint(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              :root { --probe-color: green !important; }
              :root { --probe-color: red; }
              .probe { width: 10px; height: 10px; background-color: var(--probe-color); }
            </style>
            <div class="probe"></div>`;
          const probe = document.querySelector('.probe');
          return [
            getComputedStyle(document.documentElement).getPropertyValue('--probe-color'),
            getComputedStyle(probe).getPropertyValue('--probe-color'),
            getComputedStyle(probe).backgroundColor
          ];
        })()
    )JS", "native-important-custom-property-cascade.js");
    require(value == R"JSON(["green","green","rgb(0, 128, 0)"])JSON",
        "a later normal custom property overrode an earlier important value: " + value);
}

void test_detached_style_retains_text_and_activates_when_connected(webscene_engine* engine)
{
    const auto value = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div id="detached-style-host"></div>';
          const detached = document.createElement('div');
          detached.innerHTML =
            '<style>.detached-style-probe { --detached-token: active; }</style>';
          const style = detached.firstChild;
          document.getElementById('detached-style-host').appendChild(style);
          const probe = document.createElement('div');
          probe.className = 'detached-style-probe';
          document.body.appendChild(probe);
          return [
            style.textContent.includes('--detached-token'),
            style.isConnected,
            getComputedStyle(probe).getPropertyValue('--detached-token')
          ];
        })()
    )JS", "native-detached-style-activation.js");
    require(value == R"JSON([true,true,"active"])JSON",
        "detached STYLE lost text or failed to activate after connection: " + value);
}
