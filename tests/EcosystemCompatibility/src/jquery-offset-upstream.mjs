import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;
const qunit = document.createElement("div");
qunit.id = "qunit";
fixture.parentNode.insertBefore(qunit, fixture);

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = name => name === "offset";
globalThis.moduleTeardown = () => {
  fixture.innerHTML = fixtureMarkup;
  document.documentElement.style.position = "";
  window.scrollTo(0, 0);
};

// The upstream file registers its separately served geometry fixtures through
// testIframe(). Keep those registrations visibly blocked while allowing its
// one private scrollability probe to complete against the current document.
globalThis.testIframe = (name, _path, callback, register) => {
  if (name !== null || typeof register !== "function") return;
  register("offset support probe", assert => {
    callback(assert, jQuery, window, document);
  });
};

import("../upstream/jquery/test/unit/offset.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery offset upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
