import jQuery from "jquery";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

globalThis.jQuery = globalThis.$ = jQuery;
globalThis.includesModule = () => true;
globalThis.moduleTeardown = () => {
  const fixture = document.getElementById("fixture-root");
  if (fixture) fixture.innerHTML = "";
};

import("../upstream/jquery/test/unit/callbacks.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery callbacks upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
