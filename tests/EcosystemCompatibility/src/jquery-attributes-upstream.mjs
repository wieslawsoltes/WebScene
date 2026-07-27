import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.includesModule = () => true;
globalThis.baseURL = new URL("./", document.location.href).href;
const registeredGlobals = new Set();
globalThis.Globals = {
  register(name) { registeredGlobals.add(String(name)); }
};
globalThis.moduleTeardown = () => {
  for (const name of registeredGlobals) delete globalThis[name];
  registeredGlobals.clear();
  fixture.innerHTML = fixtureMarkup;
};
globalThis.createDashboardXML = () => jQuery.parseXML(
  "<?xml version='1.0' encoding='UTF-8'?>" +
  "<dashboard><locations class='foo'><location for='bar' checked='different'>" +
  "<infowindowtab normal='ab' mixedCase='yes'><tab title='Location'><![CDATA[blabla]]></tab>" +
  "<tab title='Users'><![CDATA[blublu]]></tab></infowindowtab></location></locations></dashboard>"
);
globalThis.createXMLFragment = () => document.implementation.createDocument("", "", null).createElement("data");
globalThis.testIframe = () => {
  // The source file's final case requires jQuery's PHP mock server and a child
  // browsing context. It remains counted as harness-blocked in the manifest.
};

import("../upstream/jquery/test/unit/attributes.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery attributes upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = { status: 1, message: String(error?.message || error), stack: error?.stack || null };
    state.complete = true;
  });
