import jQuery from "jquery";
import "./jasmine-lite.mjs";
import { runRegisteredJasmineTests } from "./jasmine-lite.mjs";

globalThis.jQuery = globalThis.$ = jQuery;

import("../upstream/bootstrap/js/tests/unit/jquery.spec.js")
  .then(() => document.readyState === "loading"
    ? new Promise(resolve => document.addEventListener("DOMContentLoaded", resolve, { once: true }))
    : undefined)
  .then(() => runRegisteredJasmineTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "Bootstrap jQuery upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
