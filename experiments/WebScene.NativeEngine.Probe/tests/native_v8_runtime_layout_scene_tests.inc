void test_native_overflow_scrolling_and_nowrap(webscene_engine* engine)
{
    resize(engine, 400, 200, 40U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            .scroller { width:160px; height:64px; overflow-x:hidden; }
            .scroller.no-scrollbar::-webkit-scrollbar {
              display:none; width:0; height:0;
            }
            .hidden-scroller {
              position:absolute; left:180px; top:0; background:rgb(1, 2, 3);
            }
            .row { height:32px; }
            .title { display:block; width:80px; overflow:hidden; white-space:nowrap;
              font-size:14px; line-height:18px; }
          `;
          document.body.appendChild(style);
          const scroller = document.createElement('div');
          scroller.className = 'scroller';
          for (let index = 0; index < 5; index++) {
            const row = document.createElement('div');
            row.className = 'row';
            const title = document.createElement('span');
            title.className = 'title';
            title.appendChild(document.createTextNode(index === 0
              ? 'Arnaud Legoux Moving Average'
              : `Indicator ${index}`));
            row.appendChild(title);
            scroller.appendChild(row);
          }
          document.body.appendChild(scroller);
          globalThis.__overflowScroller = scroller;
          globalThis.__overflowScrollEvents = 0;
          scroller.addEventListener('scroll', () => __overflowScrollEvents++);
          return {
            overflowX: getComputedStyle(scroller).getPropertyValue('overflow-x'),
            overflowY: getComputedStyle(scroller).getPropertyValue('overflow-y'),
            clientHeight: scroller.clientHeight,
            scrollHeight: scroller.scrollHeight,
            scrollTop: scroller.scrollTop,
            whiteSpace: getComputedStyle(scroller.querySelector('.title')).whiteSpace,
            titleTextHeight: scroller.querySelector('.title').firstChild.getBoundingClientRect().height
          };
        })()
    )JS", "native-overflow-scroll-setup.js");
    if (initial != R"({"overflowX":"hidden","overflowY":"auto","clientHeight":64,"scrollHeight":160,"scrollTop":0,"whiteSpace":"nowrap","titleTextHeight":18})") {
        fail("overflow range or nowrap layout was not established: " + initial);
    }

    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 60, 41U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "overflow wheel input was not consumed");
    const auto scrolled = evaluate(engine, R"JS(
        (() => ({
          scrollTop: __overflowScroller.scrollTop,
          firstTop: __overflowScroller.firstElementChild.getBoundingClientRect().top,
          lastTop: __overflowScroller.children[4].getBoundingClientRect().top
        }))()
    )JS", "native-overflow-scroll-result.js");
    if (scrolled != R"({"scrollTop":60,"firstTop":-60,"lastTop":68})") {
        fail("wheel input did not scroll the nearest overflow viewport: " + scrolled);
    }

    const auto revealed = evaluate(engine, R"JS(
        (() => {
          const last = __overflowScroller.children[4];
          last.scrollIntoView({ block: 'nearest' });
          return {
            method: typeof last.scrollIntoView,
            scrollTop: __overflowScroller.scrollTop,
            firstTop: __overflowScroller.firstElementChild.getBoundingClientRect().top,
            lastTop: last.getBoundingClientRect().top
          };
        })()
    )JS", "native-scroll-into-view-result.js");
    if (revealed != R"({"method":"function","scrollTop":96,"firstTop":-96,"lastTop":32})") {
        fail("scrollIntoView did not reveal the nearest overflow item: " + revealed);
    }

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_scrollbar_rail = false;
    auto observed_scrollbar_thumb = false;
    for (auto attempt = 0; attempt < 100
        && (!observed_scrollbar_rail || !observed_scrollbar_thumb); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 10U) continue;
                if (command.rgba == 0x7F7F7F40U) {
                    observed_scrollbar_rail = std::abs(command.x - 152.0F) < 0.1F
                        && std::abs(command.y - 2.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 60.0F) < 0.1F;
                } else if (command.rgba == 0xA0A0A0D0U) {
                    observed_scrollbar_thumb = std::abs(command.x - 152.0F) < 0.1F
                        && std::abs(command.y - 38.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 24.0F) < 0.1F;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_scrollbar_rail || !observed_scrollbar_thumb) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        observed_scrollbar_rail && observed_scrollbar_thumb,
        "native overflow viewport did not paint a proportional scrollbar at its bounded maximum");

    const auto hidden_scrollbar_state = evaluate(engine, R"JS(
        (() => {
          const hiddenScroller = document.createElement('div');
          hiddenScroller.className = 'scroller no-scrollbar hidden-scroller';
          for (let index = 0; index < 5; index++) {
            const row = document.createElement('div');
            row.className = 'row';
            row.textContent = `Hidden ${index}`;
            hiddenScroller.appendChild(row);
          }
          document.body.appendChild(hiddenScroller);
          return {
            clientHeight:hiddenScroller.clientHeight,
            scrollHeight:hiddenScroller.scrollHeight
          };
        })()
    )JS", "native-hidden-scrollbar-state.js");
    require(
        hidden_scrollbar_state == R"({"clientHeight":64,"scrollHeight":160})",
        "author-hidden scrollbar changed overflow geometry: " + hidden_scrollbar_state);

    webscene_engine_request_scene_checkpoint(engine);
    auto hidden_scrollbar_scene_observed = false;
    auto hidden_scrollbar_painted = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto hidden_scroller_node_id = 0U;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.rgba == 0x010203FFU) {
                    hidden_scrollbar_scene_observed = true;
                    hidden_scroller_node_id = command.node_id;
                }
            }
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 10U
                    && (command.rgba == 0x7F7F7F40U
                        || command.rgba == 0xA0A0A0D0U)
                    && command.node_id == hidden_scroller_node_id) {
                    hidden_scrollbar_painted = true;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
            if (hidden_scrollbar_scene_observed) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        hidden_scrollbar_scene_observed,
        "author-hidden scrollbar scene was not published");
    require(
        !hidden_scrollbar_painted,
        "::-webkit-scrollbar { display:none } still painted the host overlay scrollbar");

    const auto boundary_events = evaluate(
        engine,
        "globalThis.__overflowScrollEvents",
        "native-overflow-boundary-events-before.js");
    const auto consumed_before_boundary_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 42U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_boundary_wheel + 1,
        "saturated overflow wheel input was not consumed");
    const auto bounded = evaluate(engine, R"JS(
        ({ scrollTop: __overflowScroller.scrollTop,
           scrollEvents: __overflowScrollEvents })
    )JS", "native-overflow-boundary-result.js");
    const auto expected_bounded = std::string("{\"scrollTop\":96,\"scrollEvents\":")
        + boundary_events + "}";
    require(
        bounded == expected_bounded,
        "repeated wheel input escaped the finite range or dispatched at an unchanged boundary: "
            + bounded);
}

void test_rounded_overflow_visual_fixture_geometry(webscene_engine* engine)
{
    resize(engine, 800, 600, 43U);
    const auto geometry = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `<p>instructions</p><div class="container">
            <div class="test"><div class="contents"></div></div>
            <div class="ref"></div>
          </div>`;
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = `
            div { position:absolute }
            .ref,.test { border-radius:30px }
            .ref { width:104px;height:104px;left:-2px;top:-2px;background:green }
            .test,.contents { width:100px;height:100px;background:red }
            .test { overflow:hidden }
          `;
          document.head.appendChild(style);
          const container = document.querySelector('.container').getBoundingClientRect();
          const test = document.querySelector('.test').getBoundingClientRect();
          const contents = document.querySelector('.contents').getBoundingClientRect();
          const reference = document.querySelector('.ref').getBoundingClientRect();
          return [
            test.left - container.left,
            test.top - container.top,
            contents.left - test.left,
            contents.top - test.top,
            reference.left - test.left,
            reference.top - test.top,
            test.width,
            test.height,
            reference.width,
            reference.height
          ];
        })()
    )JS", "native-rounded-overflow-visual-geometry.js");
    require(
        geometry == R"([0,0,0,0,-2,-2,100,100,104,104])",
        "rounded overflow WPT geometry diverged: " + geometry);
}

void test_row_flex_vertical_scroll_extent_remains_bounded(webscene_engine* engine)
{
    resize(engine, 400, 200, 43U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const scroller = document.createElement('div');
          scroller.style.cssText =
            'display:flex;flex-direction:row;width:160px;height:64px;overflow:auto';
          const content = document.createElement('div');
          content.style.cssText = 'flex:none;width:80px;height:160px';
          scroller.appendChild(content);
          document.body.appendChild(scroller);
          globalThis.__rowFlexScroller = scroller;
          return {
            clientHeight: scroller.clientHeight,
            scrollHeight: scroller.scrollHeight,
            scrollTop: scroller.scrollTop
          };
        })()
    )JS", "native-row-flex-overflow-setup.js");
    require(
        initial == R"({"clientHeight":64,"scrollHeight":160,"scrollTop":0})",
        "row-flex vertical overflow range was not established: " + initial);

    const auto consumed_before_first_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 43U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_first_wheel + 1,
        "row-flex boundary wheel input was not consumed");
    const auto first_boundary = evaluate(engine, R"JS(
        ({
          scrollHeight: __rowFlexScroller.scrollHeight,
          scrollTop: __rowFlexScroller.scrollTop
        })
    )JS", "native-row-flex-overflow-first-boundary.js");
    require(
        first_boundary == R"({"scrollHeight":160,"scrollTop":96})",
        "row-flex scroll extent grew after reaching its boundary: " + first_boundary);

    const auto consumed_before_second_wheel = consumed_input_count(engine);
    wheel_input(engine, 20, 20, 1000000, 44U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_second_wheel + 1,
        "repeated row-flex boundary wheel input was not consumed");
    const auto repeated_boundary = evaluate(engine, R"JS(
        ({
          scrollHeight: __rowFlexScroller.scrollHeight,
          scrollTop: __rowFlexScroller.scrollTop
        })
    )JS", "native-row-flex-overflow-repeated-boundary.js");
    require(
        repeated_boundary == R"({"scrollHeight":160,"scrollTop":96})",
        "repeated row-flex scrolling escaped the finite range: " + repeated_boundary);
}

void test_toolbar_scroll_chevrons_use_single_rotation(webscene_engine* engine)
{
    resize(engine, 320, 200, 44U);
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              body { margin:0; }
              .scrollTop {
                position:absolute; left:20px; top:20px; width:40px; height:24px;
                display:flex; align-items:center; justify-content:center;
                background:rgb(17, 34, 51);
              }
              .scrollTop .iconWrap { transform:rotate(180deg); }
              svg { width:10px; height:6px; }
            </style>
            <div class="scrollTop">
              <div class="iconWrap">
                <svg viewBox="0 0 10 6">
                  <path d="M1 1 L5 5 L9 1" fill="none" stroke="currentColor"/>
                </svg>
              </div>
            </div>`;
          const icon = document.querySelector('.iconWrap');
          return {
            transform:getComputedStyle(icon).transform,
            svgWidth:Math.abs(icon.firstElementChild.getBoundingClientRect().width - 10) < 0.001,
            svgHeight:Math.abs(icon.firstElementChild.getBoundingClientRect().height - 6) < 0.001
          };
        })()
    )JS", "native-toolbar-scroll-chevron.js");
    require(
        result == R"JSON({"transform":"matrix(-1, 0, 0, -1, 0, 0)","svgWidth":true,"svgHeight":true})JSON",
        "toolbar top-chevron layout or rotation was not established: " + result);

    webscene_engine_request_scene_checkpoint(engine);
    auto current_scene_observed = false;
    auto wrapper_rotation = false;
    auto svg_rotation = 0.0F;
    auto svg_observed = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            current_scene_observed = false;
            wrapper_rotation = false;
            svg_observed = false;
            svg_rotation = 0;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.rgba == 0x112233FFU) current_scene_observed = true;
                if (command.kind == 19U
                    && std::abs(command.stroke_width - 180.0F) < 0.1F) {
                    wrapper_rotation = true;
                }
                if (command.kind == 6U) {
                    svg_observed = true;
                    svg_rotation = command.stroke_width;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
            if (current_scene_observed) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(current_scene_observed, "toolbar top-chevron scene was not published");
    require(wrapper_rotation, "toolbar top-chevron wrapper did not emit rotate(180deg)");
    require(svg_observed, "toolbar top-chevron SVG was absent from the native scene");
    require(
        std::abs(svg_rotation) < 0.1F,
        "toolbar top-chevron inherited its wrapper rotation twice");
}

void test_root_document_overflow_scrolls_and_paints_overlay(webscene_engine* engine)
{
    resize(engine, 320, 200, 43U);
    const auto initial = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<main id="root-content"></main>';
          document.body.style.margin = '0';
          document.body.style.overflow = 'visible';
          const content = document.getElementById('root-content');
          content.style.width = '320px';
          content.style.height = '600px';
          globalThis.__rootScrollEvents = 0;
          document.body.addEventListener('scroll', () => __rootScrollEvents++);
          return {
            clientHeight: document.body.clientHeight,
            scrollHeight: document.body.scrollHeight,
            scrollTop: document.body.scrollTop
          };
        })()
    )JS", "native-root-overflow-setup.js");
    require(
        initial == R"({"clientHeight":200,"scrollHeight":600,"scrollTop":0})",
        "root document did not expose a finite viewport and content extent: " + initial);

    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 100, 100, 150, 44U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "root overflow wheel input was not consumed");
    const auto scrolled = evaluate(engine, R"JS(
        (() => ({
          scrollTop: document.body.scrollTop,
          contentTop: document.getElementById('root-content').getBoundingClientRect().top,
          scrollEvents: __rootScrollEvents
        }))()
    )JS", "native-root-overflow-result.js");
    require(
        scrolled == R"({"scrollTop":150,"contentTop":-150,"scrollEvents":1})",
        "root viewport did not scroll by a bounded wheel delta: " + scrolled);

    webscene_engine_request_scene_checkpoint(engine);
    auto observed_root_rail = false;
    auto observed_root_thumb = false;
    for (auto attempt = 0; attempt < 100
        && (!observed_root_rail || !observed_root_thumb); ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 10U) continue;
                if (command.rgba == 0x7F7F7F40U) {
                    observed_root_rail = std::abs(command.x - 312.0F) < 0.1F
                        && std::abs(command.y - 2.0F) < 0.1F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 196.0F) < 0.1F;
                } else if (command.rgba == 0xA0A0A0D0U) {
                    observed_root_thumb = std::abs(command.x - 312.0F) < 0.1F
                        && std::abs(command.y - 51.0F) < 0.6F
                        && std::abs(command.width - 6.0F) < 0.1F
                        && std::abs(command.height - 65.333F) < 0.6F;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!observed_root_rail || !observed_root_thumb) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(
        observed_root_rail && observed_root_thumb,
        "root document overflow did not paint a proportional moving scrollbar");
}

void test_table_menu_row_cells_stay_horizontal_and_centered(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            tr.menu-item { height: 32px; vertical-align: middle; }
            td.icon-cell { width: 28px; }
            .icon { width: 18px; height: 18px; }
            .content { display: flex; align-items: center; height: 20px; }
            .label { font-size: 14px; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          const body = document.createElement('tbody');
          const row = document.createElement('tr');
          row.className = 'menu-item';
          const iconCell = document.createElement('td');
          iconCell.className = 'icon-cell';
          const icon = document.createElement('div');
          icon.className = 'icon';
          iconCell.appendChild(icon);
          const contentCell = document.createElement('td');
          const content = document.createElement('div');
          content.className = 'content';
          const label = document.createElement('span');
          label.className = 'label';
          label.textContent = 'Auto (fits data to screen)';
          content.appendChild(label);
          contentCell.appendChild(content);
          row.appendChild(iconCell);
          row.appendChild(contentCell);
          body.appendChild(row);
          table.appendChild(body);
          document.body.appendChild(table);
          const rr = row.getBoundingClientRect();
          const ir = iconCell.getBoundingClientRect();
          const cr = contentCell.getBoundingClientRect();
          const lr = label.getBoundingClientRect();
          return [
            rr.height,
            ir.right <= cr.left,
            ir.top >= rr.top && ir.bottom <= rr.bottom,
            cr.top >= rr.top && cr.bottom <= rr.bottom,
            Math.abs((lr.top + lr.height / 2) - (rr.top + rr.height / 2)) < 1
          ];
        })()
    )JS", "native-table-menu-row.js");
    if (result != "[32,true,true,true,true]") {
        fail("semantic table menu row did not lay out horizontally and centrally: " + result);
    }
}

void test_semantic_table_auto_layout_and_intrinsic_cell_content(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.width = '800px';
          const style = document.createElement('style');
          style.textContent = `
            table { width: 800px; white-space: nowrap; }
            th, td { padding: 10px; }
            .content { display: inline-block; width: max-content; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          const head = document.createElement('thead');
          const groupRow = document.createElement('tr');
          const dateHeading = document.createElement('th');
          dateHeading.rowSpan = 2;
          dateHeading.textContent = 'Date · 1h';
          const priceHeading = document.createElement('th');
          priceHeading.colSpan = 5;
          priceHeading.textContent = 'AAPL · Cboe One';
          const volumeHeading = document.createElement('th');
          volumeHeading.rowSpan = 2;
          volumeHeading.textContent = 'Volume';
          groupRow.appendChild(dateHeading);
          groupRow.appendChild(priceHeading);
          groupRow.appendChild(volumeHeading);
          const labels = document.createElement('tr');
          for (const label of ['Open', 'High', 'Low', 'Close', 'Change']) {
            const cell = document.createElement('th');
            cell.textContent = label;
            labels.appendChild(cell);
          }
          head.appendChild(groupRow);
          head.appendChild(labels);
          const body = document.createElement('tbody');
          const row = document.createElement('tr');
          for (const value of ["Mon 20 Jul '26 19:30", '329.03', '329.75', '326.00', '326.71', '−2.29 (−0.70%)', '597.35 K']) {
            const cell = document.createElement('td');
            const content = document.createElement('span');
            content.className = 'content';
            content.textContent = value;
            cell.appendChild(content);
            row.appendChild(cell);
          }
          body.appendChild(row);
          table.appendChild(head);
          table.appendChild(body);
          document.body.appendChild(table);
          const rect = node => node.getBoundingClientRect();
          const bodyCells = [...row.children].map(rect);
          const labelCells = [...labels.children].map(rect);
          return {
            displays: [table, head, labels, body, row.firstElementChild]
              .map(node => getComputedStyle(node).display),
            reflectedSpans: [dateHeading.rowSpan, priceHeading.colSpan, volumeHeading.rowSpan],
            maxContentWidth: rect(row.firstElementChild.firstElementChild).width,
            rowWidth: rect(row).width,
            dateWidth: bodyCells[0].width,
            ordered: bodyCells.every((cell, index) => index === 0 || bodyCells[index - 1].right <= cell.left + 0.01),
            fillsTable: Math.abs(bodyCells.at(-1).right - rect(table).right) < 0.1,
            subgroupStartsAfterDate: Math.abs(labelCells[0].left - bodyCells[0].right) < 0.1,
            volumeStartsAfterSubgroup: Math.abs(rect(volumeHeading).left - bodyCells[6].left) < 0.1,
            rowSpanDoesNotInflateTrack:
              Math.abs(rect(head).height - rect(groupRow).height - rect(labels).height) < 0.1
              && rect(dateHeading).height > rect(groupRow).height
              && Math.abs(rect(dateHeading).height - rect(head).height) < 0.1
          };
        })()
    )JS", "native-semantic-table-auto-layout.js");
    require(
        result.find("\"displays\":[\"table\",\"table-header-group\",\"table-row\",\"table-row-group\",\"table-cell\"]")
                != std::string::npos
            && result.find(R"("reflectedSpans":[2,5,2])") != std::string::npos
            && result.find(R"("ordered":true)") != std::string::npos
            && result.find(R"("fillsTable":true)") != std::string::npos
            && result.find(R"("subgroupStartsAfterDate":true)") != std::string::npos
            && result.find(R"("volumeStartsAfterSubgroup":true)") != std::string::npos
            && result.find(R"("rowSpanDoesNotInflateTrack":true)") != std::string::npos,
        "semantic auto-layout table did not preserve its shared column grid: " + result);
    const auto max_content_marker = result.find(R"("maxContentWidth":)");
    require(max_content_marker != std::string::npos, "max-content table metric was absent: " + result);
    const auto max_content_width = std::strtod(
        result.c_str() + max_content_marker + std::strlen(R"("maxContentWidth":)"),
        nullptr);
    require(max_content_width > 100, "max-content table text collapsed: " + result);
}

void test_fixed_table_distributes_excess_after_percentage_columns(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const style = document.createElement('style');
          style.textContent = `
            table.fixed-distribution { width: 300px; border-collapse: collapse; table-layout: fixed; height: 20px; }
            .fixed-distribution td { padding: 0; }
            .fixed-distribution td:nth-child(1) { width: 20px; }
            .fixed-distribution td:nth-child(2) { width: 10px; }
            .fixed-distribution td:nth-child(3) { width: 10%; }
          `;
          document.body.appendChild(style);
          const table = document.createElement('table');
          table.className = 'fixed-distribution';
          const row = document.createElement('tr');
          for (let index = 0; index < 3; index++) row.appendChild(document.createElement('td'));
          table.appendChild(row);
          document.body.appendChild(table);
          return {
            table: table.offsetWidth,
            row: row.offsetWidth,
            cells: Array.from(row.children, cell => cell.offsetWidth)
          };
        })()
    )JS", "native-fixed-table-width-distribution.js");
    require(
        result == R"({"table":300,"row":300,"cells":[180,90,30]})",
        "fixed table excess-width distribution regressed: " + result);
}

void test_implicit_grid_contains_scrollable_table(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const shell = document.createElement('div');
          shell.style.display = 'flex';
          shell.style.flexDirection = 'column';
          shell.style.width = '800px';
          shell.style.height = '600px';
          const header = document.createElement('div');
          header.style.height = '50px';
          header.style.flexShrink = '0';
          const grid = document.createElement('div');
          grid.style.display = 'grid';
          grid.style.flexGrow = '1';
          grid.style.overflow = 'hidden';
          const center = document.createElement('div');
          center.style.height = '100%';
          center.style.overflow = 'hidden';
          const scroller = document.createElement('div');
          scroller.style.maxHeight = '100%';
          scroller.style.overflow = 'auto';
          const table = document.createElement('table');
          table.style.width = '100%';
          const body = document.createElement('tbody');
          for (let index = 0; index < 40; index++) {
            const row = document.createElement('tr');
            const cell = document.createElement('x-cell');
            cell.style.display = 'table-cell';
            cell.style.height = '30px';
            cell.textContent = `Row ${index}`;
            row.appendChild(cell);
            body.appendChild(row);
          }
          table.appendChild(body);
          scroller.appendChild(table);
          center.appendChild(scroller);
          grid.appendChild(center);
          shell.appendChild(header);
          shell.appendChild(grid);
          document.body.appendChild(shell);
          const rect = node => node.getBoundingClientRect();
          return {
            shell: rect(shell).height,
            header: rect(header).height,
            grid: rect(grid).height,
            center: rect(center).height,
            scroller: rect(scroller).height,
            table: rect(table).height,
            scrollHeight: scroller.scrollHeight
          };
        })()
    )JS", "native-grid-table-overflow.js");
    require(
        result.find(R"("shell":600)") != std::string::npos
            && result.find(R"("header":50)") != std::string::npos
            && result.find(R"("grid":550)") != std::string::npos
            && result.find(R"("center":550)") != std::string::npos
            && result.find(R"("scroller":550)") != std::string::npos
            && result.find(R"("table":1200)") != std::string::npos
            && result.find(R"("scrollHeight":1200)") != std::string::npos,
        "implicit grid did not contain its scrollable table: " + result);
}

void test_auto_height_flex_popup_expands_overflowing_flex_child(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const popup = document.createElement('div');
          popup.style.display = 'flex';
          popup.style.width = '344px';
          const content = document.createElement('div');
          content.style.display = 'flex';
          content.style.flex = '1 1 auto';
          content.style.flexDirection = 'column';
          content.style.overflow = 'hidden';
          const summary = document.createElement('div');
          summary.style.height = '80px';
          const details = document.createElement('div');
          details.style.height = '56px';
          content.appendChild(summary);
          content.appendChild(details);
          popup.appendChild(content);
          document.body.appendChild(popup);
          return [
            popup.getBoundingClientRect().height,
            content.getBoundingClientRect().height,
            details.getBoundingClientRect().bottom <= content.getBoundingClientRect().bottom
          ];
        })()
    )JS", "native-auto-height-flex-popup.js");
    require(
        result == "[136,136,true]",
        "an auto-height flex popup clipped its overflowing flex child: " + result);
}

void test_constrained_column_flex_scroll_item_keeps_footer_inside(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const host = document.createElement('div');
          host.style.display = 'flex';
          host.style.flexDirection = 'column';
          host.style.height = '200px';
          host.style.width = '300px';
          const header = document.createElement('div');
          header.style.flexGrow = '0';
          header.style.flexShrink = '0';
          header.style.height = '60px';
          const content = document.createElement('div');
          content.style.overflow = 'auto';
          const tall = document.createElement('div');
          tall.style.height = '200px';
          content.appendChild(tall);
          const footer = document.createElement('div');
          footer.style.flexGrow = '0';
          footer.style.flexShrink = '0';
          footer.style.height = '60px';
          host.appendChild(header);
          host.appendChild(content);
          host.appendChild(footer);
          document.body.appendChild(host);
          const hr = host.getBoundingClientRect();
          const cr = content.getBoundingClientRect();
          const fr = footer.getBoundingClientRect();
          return [hr.height, cr.height, content.scrollHeight, fr.bottom <= hr.bottom];
        })()
    )JS", "native-constrained-column-flex-scroll.js");
    require(
        result == "[200,80,200,true]",
        "a constrained column flex scroller expanded through its footer: " + result);
}

void test_later_dom_overlay_background_paints_above_retained_canvas(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const chartHost = document.createElement('div');
          const canvas = document.createElement('canvas');
          canvas.width = 80;
          canvas.height = 40;
          canvas.style.width = '80px';
          canvas.style.height = '40px';
          canvas.getContext('2d').fillRect(0, 0, 80, 40);
          chartHost.appendChild(canvas);
          const elevatedChartChild = document.createElement('div');
          elevatedChartChild.style.position = 'relative';
          elevatedChartChild.style.zIndex = '100';
          elevatedChartChild.style.color = '#abcdef';
          elevatedChartChild.textContent = 'retained chart legend';
          chartHost.appendChild(elevatedChartChild);
          document.body.appendChild(chartHost);
          const overlayHost = document.createElement('div');
          const overlay = document.createElement('div');
          overlay.id = 'retained-overlay';
          overlay.style.width = '80px';
          overlay.style.height = '40px';
          overlay.style.backgroundColor = '#2468ac';
          overlayHost.appendChild(overlay);
          const elevatedOverlayChild = document.createElement('div');
          elevatedOverlayChild.style.position = 'relative';
          elevatedOverlayChild.style.zIndex = '8';
          overlayHost.appendChild(elevatedOverlayChild);
          document.body.appendChild(overlayHost);
        })()
    )JS", "native-retained-canvas-overlay.js");
    require(
        evaluate(engine, "document.querySelector('#retained-overlay').offsetWidth", "native-retained-canvas-overlay-ready.js") == "80",
        "retained-canvas overlay fixture did not lay out");

    webscene_engine_request_scene_checkpoint(engine);
    auto foreground_overlay = false;
    auto overlay_covers_earlier_chart_dom = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            auto chart_text_index = scene->header.command_count;
            auto overlay_index = scene->header.command_count;
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 3U && (command.rgba >> 8U) == 0xABCDEFU) {
                    chart_text_index = index;
                }
                if (command.kind == 9U && (command.rgba >> 8U) == 0x2468ACU) {
                    foreground_overlay = true;
                    overlay_index = index;
                }
            }
            overlay_covers_earlier_chart_dom = chart_text_index < overlay_index
                && overlay_index < scene->header.command_count;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (foreground_overlay && overlay_covers_earlier_chart_dom) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(foreground_overlay, "a later DOM overlay background remained behind its retained canvas");
    require(
        overlay_covers_earlier_chart_dom,
        "a descendant z-index reordered an earlier ancestor above a later DOM overlay");
}

void test_shadow_dom_composed_runtime_geometry(webscene_engine* engine)
{
    resize(engine, 800, 600, 86U);
    const auto geometry = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const style = document.createElement('style');
          style.textContent = '.title{font-weight:700}.detail{display:block;color:rgb(72,82,100)}';
          document.body.appendChild(style);
          const host = document.createElement('div');
          const title = document.createElement('div');
          title.className = 'title';
          title.slot = 'title';
          title.textContent = 'Ready component';
          const detail = document.createElement('div');
          detail.className = 'detail';
          detail.textContent = 'Slotted content follows one composed layout and paint tree.';
          host.appendChild(title);
          host.appendChild(detail);
          document.body.appendChild(host);
          const root = host.attachShadow({mode:'open'});
          root.innerHTML = `<style>
            :host{display:block;width:360px;margin:24px}
            .card{display:flex;width:360px;padding:16px;gap:12px}
            .badge{flex:0 0 40px;height:40px;background:rgb(44,84,164)}
            .copy{flex:1 1 auto;min-width:0} slot,.fallback{display:block}
          </style><div class="card"><div class="badge"></div><div class="copy">
            <slot name="title"></slot><slot><span class="fallback">Fallback</span></slot>
            </div></div>`;
          const slots = root.querySelectorAll('slot');
          const fallback = root.querySelector('.fallback');
          const titleRect = title.getBoundingClientRect();
          const detailRect = detail.getBoundingClientRect();
          const fallbackWhileAssigned = fallback.getBoundingClientRect();
          detail.slot = 'missing';
          const hiddenDetailRect = detail.getBoundingClientRect();
          const visibleFallbackRect = fallback.getBoundingClientRect();
          detail.removeAttribute('slot');
          const restoredDetailRect = detail.getBoundingClientRect();
          const hiddenFallbackRect = fallback.getBoundingClientRect();
          return slots[0].assignedElements().length === 1
            && slots[1].assignedElements().length === 1
            && titleRect.width > 0 && titleRect.height > 0
            && detailRect.width > 0 && detailRect.height > 0
            && fallbackWhileAssigned.width === 0 && fallbackWhileAssigned.height === 0
            && hiddenDetailRect.width === 0 && hiddenDetailRect.height === 0
            && visibleFallbackRect.width > 0 && visibleFallbackRect.height > 0
            && restoredDetailRect.width > 0 && restoredDetailRect.height > 0
            && hiddenFallbackRect.width === 0 && hiddenFallbackRect.height === 0;
        })()
    )JS", "native-shadow-composed-scene-setup.js");
    require(
        geometry == "true",
        "shadow slot projection did not establish composed geometry: " + geometry);
}
