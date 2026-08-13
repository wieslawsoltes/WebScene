import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = name => name === "deprecated" || name === "selector";
globalThis.moduleTeardown = () => {
  fixture.innerHTML = fixtureMarkup;
};

import("../upstream/jquery/test/unit/deprecated.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery deprecated upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
