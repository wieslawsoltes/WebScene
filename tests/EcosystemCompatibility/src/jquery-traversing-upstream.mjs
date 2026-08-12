import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
document.documentElement.id = "html";
document.body.id = "body";
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.baseURL = new URL("./", document.location.href).href;
globalThis.QUnit.jQuerySelectors = true;
globalThis.QUnit.jQuerySelectorsPos = false;
globalThis.q = (...ids) => ids.map(id => document.getElementById(id));
globalThis.__webSceneQUnitBlockedNames = new Set([
  "contents()",
  "contents() for <object />",
  "contents() for <frame />"
]);

const registeredGlobals = new Set();
globalThis.Globals = {
  register(name) {
    const key = String(name);
    globalThis[key] = true;
    registeredGlobals.add(key);
  },
  cleanup() {
    for (const name of registeredGlobals) delete globalThis[name];
    registeredGlobals.clear();
  }
};
globalThis.moduleTeardown = () => {
  globalThis.Globals.cleanup();
  fixture.innerHTML = fixtureMarkup;
};

import("../upstream/jquery/test/unit/traversing.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery traversing upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
