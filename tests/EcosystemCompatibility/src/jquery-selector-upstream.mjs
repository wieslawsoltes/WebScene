import jQuery from "jquery";
import fixtureMarkup from "../upstream/jquery/test/data/qunit-fixture.html";
import "./qunit-lite.mjs";
import { runRegisteredQUnitTests } from "./qunit-lite.mjs";

const fixture = document.getElementById("fixture-root");
document.documentElement.id = "html";
document.documentElement.setAttribute("lang", "en");
document.body.id = "body";
fixture.id = "qunit-fixture";
fixture.innerHTML = fixtureMarkup;

globalThis.jQuery = globalThis.$ = globalThis.supportjQuery = jQuery;
globalThis.baseURL = new URL("./", document.location.href).href;
globalThis.QUnit.jQuerySelectors = true;
globalThis.QUnit.jQuerySelectorsPos = true;
globalThis.q = (...ids) => ids.map(id => document.getElementById(id));
globalThis.__webSceneQUnitBlockedNames = new Set([
  "Iframe dispatch should not affect jQuery (trac-13936)",
  "pseudo - :(dis|en)abled, optgroup and option"
]);

globalThis.QUnit.assert.t = function(message, selector, expectedIds) {
  this.deepEqual(
    jQuery(selector).get(),
    globalThis.q(...expectedIds),
    `${message} (${selector})`
  );
};
globalThis.QUnit.assert.selectInFixture = function(message, selector, expectedIds) {
  this.deepEqual(
    jQuery(selector, fixture).get(),
    globalThis.q(...expectedIds),
    `${message} (${selector})`
  );
};

globalThis.createWithFriesXML = () => jQuery.parseXML(
  "<?xml version='1.0' encoding='UTF-8'?>" +
  "<soap:Envelope xmlns:soap='http://schemas.xmlsoap.org/soap/envelope/' " +
  "xmlns:xsd='http://www.w3.org/2001/XMLSchema' " +
  "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'>" +
  "<soap:Body><jsconf xmlns='http://www.example.com/ns1'>" +
  "<response xmlns:ab='http://www.example.com/ns2'><meta>" +
  "<component id='seite1' class='component'>" +
  "<properties xmlns:cd='http://www.example.com/ns3'>" +
  "<property name='prop1'><thing /><value>1</value></property>" +
  "<property name='prop2'><thing att='something' /></property>" +
  "<foo_bar>foo</foo_bar></properties></component>" +
  "</meta></response></jsconf></soap:Body></soap:Envelope>"
);

globalThis.testIframe = () => {
  // Three registrations require separately served selector iframe fixtures.
  // They remain explicitly harness-blocked in the source inventory.
};
globalThis.moduleTeardown = () => {
  fixture.innerHTML = fixtureMarkup;
};

import("../upstream/jquery/test/unit/selector.js")
  .then(() => runRegisteredQUnitTests())
  .catch(error => {
    const state = globalThis.__webSceneWptState;
    state.results.push({
      name: "jQuery selector upstream source load",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
    state.harness = {
      status: 1,
      message: String(error?.message || error),
      stack: error?.stack || null
    };
    state.complete = true;
  });
