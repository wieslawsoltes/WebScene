import { state } from "./harness.mjs";

const rootModule = createModule("");
let currentModule = rootModule;
let registrationIndex = 0;

function createModule(name, hooks = {}, parent = null) {
  return { name, hooks, parent, children: [] };
}

function registerModule(name, options, body) {
  if (typeof options === "function") {
    body = options;
    options = {};
  }
  const parent = typeof body === "function" ? currentModule : rootModule;
  const module = createModule(String(name), options || {}, parent);
  parent.children.push({ type: "module", module });
  if (typeof body !== "function") {
    currentModule = module;
    return;
  }
  const previous = currentModule;
  currentModule = module;
  try {
    body({
      beforeEach(callback) { module.hooks.beforeEach = callback; },
      afterEach(callback) { module.hooks.afterEach = callback; }
    });
  } finally {
    currentModule = previous;
  }
}

function registerTest(name, body, skipped = false) {
  const testName = String(name);
  const includes = (selection, value) => typeof selection?.has === "function"
    ? selection.has(value)
    : Array.isArray(selection) && selection.includes(value);
  const blocked = globalThis.__webSceneQUnitBlockedNames;
  if (blocked && includes(blocked, testName)) return;
  const only = globalThis.__webSceneQUnitOnlyNames;
  if (only && !includes(only, testName)) return;
  const excluded = globalThis.__webSceneQUnitExcludedNames;
  if (excluded && includes(excluded, testName)) return;
  const index = registrationIndex++;
  const shard = globalThis.__webSceneQUnitShard;
  if (shard && index % Number(shard.count) !== Number(shard.index)) return;
  currentModule.children.push({ type: "test", name: testName, body, skipped });
}

const QUnit = globalThis.QUnit = {
  module: registerModule,
  test: registerTest,
  testUnlessIE: registerTest,
  skip(name, body) { registerTest(name, body, true); },
  isIE: false,
  assert: {}
};

function describe(value) {
  if (typeof value === "string") return JSON.stringify(value);
  if (value === undefined) return "undefined";
  if (value === null) return "null";
  try { return JSON.stringify(value); } catch { return String(value); }
}

function valuesDeepEqual(actual, expected, seen = new Map()) {
  if (Object.is(actual, expected)) return true;
  const actualBoxed = actual instanceof String || actual instanceof Number || actual instanceof Boolean;
  const expectedBoxed = expected instanceof String || expected instanceof Number || expected instanceof Boolean;
  if (actualBoxed || expectedBoxed) {
    return Object.is(
      actualBoxed ? actual.valueOf() : actual,
      expectedBoxed ? expected.valueOf() : expected
    );
  }
  if (actual === null || expected === null
      || typeof actual !== "object" || typeof expected !== "object") return false;
  if (seen.get(actual) === expected) return true;
  seen.set(actual, expected);
  if (Array.isArray(actual) || Array.isArray(expected)) {
    return Array.isArray(actual) && Array.isArray(expected)
      && actual.length === expected.length
      && actual.every((value, index) => valuesDeepEqual(value, expected[index], seen));
  }
  const actualKeys = Object.keys(actual);
  const expectedKeys = Object.keys(expected);
  return actualKeys.length === expectedKeys.length
    && expectedKeys.every(key => Object.hasOwn(actual, key)
      && valuesDeepEqual(actual[key], expected[key], seen));
}

function describeDeepDifference(actual, expected) {
  if (actual === null || expected === null
      || typeof actual !== "object" || typeof expected !== "object") return "";
  const actualKeys = Object.keys(actual);
  const expectedKeys = Object.keys(expected);
  const missing = expectedKeys.filter(key => !Object.hasOwn(actual, key));
  const extra = actualKeys.filter(key => !Object.hasOwn(expected, key));
  const unequal = expectedKeys.filter(key => Object.hasOwn(actual, key)
    && !valuesDeepEqual(actual[key], expected[key]));
  return `; actualKeys=${JSON.stringify(actualKeys)} expectedKeys=${JSON.stringify(expectedKeys)}`
    + ` missing=${JSON.stringify(missing)} extra=${JSON.stringify(extra)} unequal=${JSON.stringify(unequal)}`;
}

function createAssert(testName) {
  let expectedCount = null;
  let assertionCount = 0;
  let pending = 0;
  let settle = null;
  const failures = [];

  const record = (condition, message) => {
    assertionCount++;
    if (!condition) failures.push(
      new Error(message || `QUnit assertion failed in ${testName}`));
  };
  const assert = {
    expect(count) { expectedCount = Number(count); },
    ok(value, message) { record(Boolean(value), message || `expected ${describe(value)} to be truthy`); },
    notOk(value, message) { record(!value, message || `expected ${describe(value)} to be falsy`); },
    equal(actual, expected, message) {
      record(actual == expected,
        `${message ? `${message}: ` : ""}expected ${describe(actual)} to loosely equal ${describe(expected)}`);
    },
    notEqual(actual, expected, message) {
      record(actual != expected, message || `expected ${describe(actual)} not to loosely equal ${describe(expected)}`);
    },
    strictEqual(actual, expected, message) {
      record(actual === expected,
        `${message ? `${message}: ` : ""}expected ${describe(actual)} to strictly equal ${describe(expected)}`);
    },
    notStrictEqual(actual, expected, message) {
      record(actual !== expected, message || `expected ${describe(actual)} not to strictly equal ${describe(expected)}`);
    },
    deepEqual(actual, expected, message) {
      record(
        valuesDeepEqual(actual, expected),
        `${message ? `${message}: ` : ""}expected ${describe(actual)} to deeply equal ${describe(expected)}`
          + describeDeepDifference(actual, expected)
      );
    },
    async(count = 1) {
      let remaining = Number(count) || 1;
      pending += remaining;
      return () => {
        if (remaining <= 0) return;
        remaining--;
        pending--;
        if (pending === 0 && settle) settle();
      };
    },
    throws(callback, expected, message) {
      if (typeof expected === "string" && message === undefined) {
        message = expected;
        expected = undefined;
      }
      let thrown = null;
      try { callback(); } catch (error) { thrown = error; }
      const matches = thrown !== null && (
        expected === undefined
        || (expected instanceof RegExp && expected.test(String(thrown?.message || thrown)))
        || (typeof expected === "function" && thrown instanceof expected));
      record(matches, message || "expected callback to throw");
    }
  };
  for (const [name, extension] of Object.entries(QUnit.assert)) {
    if (!(name in assert) && typeof extension === "function") {
      assert[name] = (...args) => extension.apply(assert, args);
    }
  }
  return {
    assert,
    async wait() {
      if (pending > 0) {
        await Promise.race([
          new Promise(resolve => { settle = resolve; }),
          new Promise((_, reject) => setTimeout(
            () => reject(new Error(`QUnit-compatible test timed out after 5000 ms: ${testName}`)), 5000))
        ]);
      }
      if (expectedCount !== null && assertionCount !== expectedCount) {
        throw new Error(`${testName}: expected ${expectedCount} assertions, observed ${assertionCount}`);
      }
      if (failures.length) throw failures[0];
    }
  };
}

function moduleLineage(module) {
  const result = [];
  for (let current = module; current && current !== rootModule; current = current.parent) {
    result.unshift(current);
  }
  return result;
}

async function executeTest(module, test) {
  const lineage = moduleLineage(module);
  const name = [...lineage.map(item => item.name), test.name].join(" ");
  if (test.skipped) {
    state.results.push({ name, status: "PASS", message: "SKIP", stack: null });
    return;
  }
  const context = {};
  const assertion = createAssert(name);
  const registeredAssertions = QUnit.assert;
  QUnit.assert = assertion.assert;
  try {
    for (const item of lineage) await item.hooks.beforeEach?.call(context, assertion.assert);
    await test.body.call(context, assertion.assert);
    await assertion.wait();
    state.results.push({ name, status: "PASS", message: null, stack: null });
  } catch (error) {
    state.results.push({
      name,
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
  } finally {
    for (let index = lineage.length - 1; index >= 0; index--) {
      try {
        await lineage[index].hooks.afterEach?.call(context, assertion.assert);
      } catch (error) {
        state.results.push({
          name: `${name} afterEach`,
          status: "FAIL",
          message: String(error?.message || error),
          stack: error?.stack ? String(error.stack) : null
        });
      }
    }
    QUnit.assert = registeredAssertions;
  }
}

async function executeModule(module) {
  for (const child of module.children) {
    if (child.type === "module") await executeModule(child.module);
    else await executeTest(module, child);
  }
}

export async function runRegisteredQUnitTests() {
  try {
    await executeModule(rootModule);
  } catch (error) {
    state.results.push({
      name: "QUnit-compatible suite lifecycle",
      status: "FAIL",
      message: String(error?.message || error),
      stack: error?.stack ? String(error.stack) : null
    });
  }
  const failure = state.results.find(result => result.status !== "PASS");
  const error = state.errors[0];
  state.harness = failure || error
    ? { status: 1, message: failure?.message || error, stack: failure?.stack || null }
    : { status: 0, message: null, stack: null };
  state.complete = true;
}
