import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = name => name === "css" || name === "offset";
globalThis.baseURL = new URL("./", document.location.href).href;
globalThis.QUnit.jQuerySelectors = true;
globalThis.QUnit.jQuerySelectorsPos = false;
globalThis.__webSceneQUnitBlockedNames = new Set([
  "show/hide shadow child nodes",
  "shadow toggle()"
]);

const registeredGlobals = new Set();
globalThis.Globals = {
  register(name) { registeredGlobals.add(String(name)); },
  cleanup() {
    for (const name of registeredGlobals) delete globalThis[name];
    registeredGlobals.clear();
  }
};
globalThis.moduleTeardown = () => {
  globalThis.Globals.cleanup();
  fixture.innerHTML = fixtureMarkup;
};
globalThis.testIframe = () => {
  // Three upstream cases require jQuery's separately served iframe fixtures.
  // They remain counted as in-file harness-blocked in the source manifest.
};
globalThis.QUnit.assert.t = function(message, selector, expectedIds) {
  const actualIds = jQuery(selector).get().map(element => element.id);
  this.deepEqual(actualIds, expectedIds, `${message} (${selector})`);
};

import("../upstream/jquery/test/unit/css.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery CSS upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
