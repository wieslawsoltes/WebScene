import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;
for (const id of ["qunit-header", "qunit-banner", "qunit-userAgent", "qunit-tests"]) {
  const scaffold = document.createElement(id === "qunit-tests" ? "ol" : "div");
  scaffold.id = id;
  fixture.parentNode.insertBefore(scaffold, fixture);
}
const toolbar = document.createElement("div");
toolbar.id = "qunit-testrunner-toolbar";
fixture.parentNode.insertBefore(toolbar, fixture);

globalThis.originaljQuery = undefined;
globalThis.original$ = undefined;
globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = () => true;
globalThis.baseURL = new URL("./", document.location.href).href;
globalThis.QUnit.jQuerySelectors = true;
globalThis.q = (...ids) => ids.map(id => document.getElementById(id));
globalThis.createDashboardXML = () => jQuery.parseXML(
  "<?xml version='1.0' encoding='UTF-8'?>" +
  "<dashboard><locations class='foo'><location for='bar' checked='different'>" +
  "<infowindowtab normal='ab' mixedCase='yes'><tab title='Location'><![CDATA[blabla]]></tab>" +
  "<tab title='Users'><![CDATA[blublu]]></tab></infowindowtab></location></locations></dashboard>"
);

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

const stubs = [];
globalThis.sinon = {
  createSandbox() {
    return {
      stub(target, property) {
        const original = target[property];
        const stub = function(...args) {
          const call = { args, lastArg: args.at(-1) };
          stub.calls.push(call);
          stub.firstCall ||= call;
          return stub.fake?.apply(this, args);
        };
        stub.calls = [];
        stub.callsFake = fake => {
          stub.fake = fake;
          target[property] = stub;
          return stub;
        };
        target[property] = stub;
        stubs.push(() => { target[property] = original; });
        return stub;
      },
      restore() {
        while (stubs.length) stubs.pop()();
      }
    };
  }
};

globalThis.__webSceneQUnitBlockedNames = new Set([
  "globalEval execution after script injection (trac-7862)"
]);
globalThis.moduleTeardown = () => {
  globalThis.Globals.cleanup();
  fixture.innerHTML = fixtureMarkup;
};
globalThis.testIframe = () => {
  // Five registrations require separately served core iframe fixtures.
  // They remain explicitly harness-blocked in the source inventory.
};

import("../upstream/jquery/test/unit/core.js")
  .then(() => new Promise(resolve => jQuery(resolve)))
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery core upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
