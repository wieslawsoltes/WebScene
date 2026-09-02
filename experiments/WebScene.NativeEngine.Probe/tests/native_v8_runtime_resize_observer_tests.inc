// Exercise real host resize input, not a JS DPR shim. These regressions were
// first verified red against the CSS-pixel alias implementation.
void resize_observer_test_frame(webscene_engine* engine, double scale, uint64_t sequence)
{
    const auto before = consumed_input_count(engine);
    resize(engine, 457, 552, sequence, scale);
    wait_for_consumed_inputs(engine, before + 1U, "observer test resize was not consumed");
    animation_frame_and_wait(engine, static_cast<double>(sequence), sequence + 1U);
}

void test_resize_observer_device_pixel_canvas_export(webscene_engine* engine)
{
    resize_observer_test_frame(engine, 2.0, 22000U);
    execute(engine, R"JS(
      (() => {
      document.body.innerHTML = '<canvas id="observed-pane" style="display:block;width:331px;height:443px"></canvas>';
      globalThis.__pixelBoxResult = null;
      globalThis.__pixelBoxObserver = new ResizeObserver(entries => {
        const entry = entries[0];
        const pane = entry.target;
        const physical = entry.devicePixelContentBoxSize[0];
        pane.width = physical.inlineSize;
        pane.height = physical.blockSize;
        const axis = document.createElement('canvas');
        axis.width = 70 * devicePixelRatio;
        axis.height = 443 * devicePixelRatio;
        const exported = document.createElement('canvas');
        exported.width = pane.width + axis.width + 20 * devicePixelRatio;
        exported.height = pane.height + 98 * devicePixelRatio;
        exported.getContext('2d').drawImage(pane, 20, 20);
        exported.getContext('2d').drawImage(axis, 20 + pane.width, 20);
        __pixelBoxResult = {
          dpr: devicePixelRatio,
          css: [entry.contentBoxSize[0].inlineSize, entry.contentBoxSize[0].blockSize],
          pane: [pane.width, pane.height],
          axis: [axis.width, axis.height],
          exported: [exported.width, exported.height]
        };
      });
      __pixelBoxObserver.observe(document.getElementById('observed-pane'), {box:'device-pixel-content-box'});
      })();
    )JS", "resize-observer-retina-export-setup.js");
    animation_frame_and_wait(engine, 22002.0, 22002U);
    const auto actual = evaluate(engine, R"JS(
      (() => { __pixelBoxObserver.disconnect(); return __pixelBoxResult; })()
    )JS", "resize-observer-retina-export-result.js");
    require(actual == R"JSON({"dpr":2,"css":[331,443],"pane":[662,886],"axis":[140,886],"exported":[842,1082]})JSON",
        "Retina export mixed CSS-sized pane with device-pixel axis: " + actual);
    resize_observer_test_frame(engine, 1.0, 22004U);
}

void test_resize_observer_device_scale_only_delivery(webscene_engine* engine)
{
    resize_observer_test_frame(engine, 1.0, 22100U);
    execute(engine, R"JS(
      (() => {
      document.body.innerHTML = '<canvas id="scale-pane" style="display:block;width:100px;height:60px"></canvas>';
      globalThis.__deviceDeliveries = [];
      globalThis.__cssDeliveries = [];
      const pane = document.getElementById('scale-pane');
      globalThis.__deviceObserver = new ResizeObserver(entries => {
        const size = entries[0].devicePixelContentBoxSize[0];
        __deviceDeliveries.push([devicePixelRatio, size.inlineSize, size.blockSize]);
      });
      globalThis.__cssObserver = new ResizeObserver(entries => {
        const size = entries[0].contentBoxSize[0];
        __cssDeliveries.push([size.inlineSize, size.blockSize]);
      });
      __deviceObserver.observe(pane, {box:'device-pixel-content-box'});
      __cssObserver.observe(pane, {box:'content-box'});
      })();
    )JS", "resize-observer-scale-transition-setup.js");
    animation_frame_and_wait(engine, 22102.0, 22102U);
    // No CSS geometry changes: moving a window between displays must still
    // resize the device-pixel backing store, but not notify content-box users.
    resize_observer_test_frame(engine, 2.0, 22104U);
    resize_observer_test_frame(engine, 1.5, 22106U);
    resize_observer_test_frame(engine, 1.0, 22108U);
    resize_observer_test_frame(engine, 1.0, 22110U);
    const auto actual = evaluate(engine, R"JS(
      (() => {
        __deviceObserver.disconnect(); __cssObserver.disconnect();
        return {device:__deviceDeliveries, css:__cssDeliveries};
      })()
    )JS", "resize-observer-scale-transition-result.js");
    require(actual == R"JSON({"device":[[1,100,60],[2,200,120],[1.5,150,90],[1,100,60]],"css":[[100,60]]})JSON",
        "Device-pixel observer missed scale-only changes or CSS observer duplicated delivery: " + actual);
}
