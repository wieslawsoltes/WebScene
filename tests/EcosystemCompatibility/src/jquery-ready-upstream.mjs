import jQuery from "jquery";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const domReady = document.readyState === "loading"
  ? new Promise(resolve => document.addEventListener("DOMContentLoaded", resolve, { once: true }))
  : Promise.resolve();

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = name => name === "ready" || name === "deferred";
globalThis.__webSceneWptState.expectedRuntimeExceptions = ["Ready error "];

import("../upstream/jquery/test/unit/ready.js")
  .then(async () => {
    await domReady;
    await new Promise(resolve => jQuery.ready.then(() => setTimeout(resolve)));
    await runRegisteredQUnitTests();
  })
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery ready upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
