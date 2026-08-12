import { state } from "./harness.mjs";

const rootSuite = createSuite("");
let currentSuite = rootSuite;
let activeSpies = [];
let lastExpectationFailure = null;
const specTaskDrainMilliseconds = 10;

function createSuite(name) {
  return { name, beforeAll: [], beforeEach: [], afterEach: [], afterAll: [], children: [] };
}

function registerSuite(name, body) {
  const parent = currentSuite;
  const suite = createSuite(name);
  parent.children.push({ type: "suite", suite });
  currentSuite = suite;
  try {
    body();
  } finally {
    currentSuite = parent;
  }
}

function registerTest(name, body) {
  currentSuite.children.push({ type: "test", name, body });
}

globalThis.describe = registerSuite;
globalThis.it = registerTest;
globalThis.beforeAll = body => currentSuite.beforeAll.push(body);
globalThis.beforeEach = body => currentSuite.beforeEach.push(body);
globalThis.afterEach = body => currentSuite.afterEach.push(body);
globalThis.afterAll = body => currentSuite.afterAll.push(body);

const anyMarker = Symbol("jasmine.any");
const objectContainingMarker = Symbol("jasmine.objectContaining");
globalThis.jasmine = {
  any(constructor) {
    return { [anyMarker]: constructor };
  },
  objectContaining(sample) {
    return { [objectContainingMarker]: sample };
  },
  createSpy() {
    return createSpyFunction();
  }
};

function describeValue(value) {
  if (typeof value === "string") return JSON.stringify(value);
  if (value === null) return "null";
  if (value === undefined) return "undefined";
  return String(value);
}

function describeElementState(element) {
  if (!element || element.nodeType !== 1) return "";
  let display = "<unavailable>";
  let visibility = "<unavailable>";
  let clientRects = "<unavailable>";
  try {
    const computed = getComputedStyle(element);
    display = computed.getPropertyValue("display");
    visibility = computed.getPropertyValue("visibility");
    clientRects = String(element.getClientRects().length);
  } catch { /* Diagnostics must never alter matcher behavior. */ }
  return `; element=${element.tagName || element.nodeName}`
    + `#${element.id || ""}.${String(element.className || "").replaceAll(" ", ".")}`
    + ` display=${JSON.stringify(display)}`
    + ` visibility=${JSON.stringify(visibility)}`
    + ` clientRects=${clientRects}`;
}

function matchesEqual(actual, expected) {
  if (expected?.[anyMarker]) {
    if (expected[anyMarker] === String) return typeof actual === "string" || actual instanceof String;
    if (expected[anyMarker] === Number) return typeof actual === "number" || actual instanceof Number;
    if (expected[anyMarker] === Boolean) return typeof actual === "boolean" || actual instanceof Boolean;
    return actual instanceof expected[anyMarker];
  }
  if (expected?.[objectContainingMarker]) {
    return actual !== null && typeof actual === "object"
      && Object.entries(expected[objectContainingMarker])
        .every(([key, value]) => matchesEqual(actual[key], value));
  }
  if (Object.is(actual, expected)) return true;
  if (Array.isArray(actual) || Array.isArray(expected)) {
    return Array.isArray(actual) && Array.isArray(expected)
      && actual.length === expected.length
      && actual.every((value, index) => matchesEqual(value, expected[index]));
  }
  if (actual === null || expected === null
      || typeof actual !== "object" || typeof expected !== "object"
      || actual.nodeType !== undefined || expected.nodeType !== undefined) return false;
  const actualKeys = Object.keys(actual);
  const expectedKeys = Object.keys(expected);
  return actualKeys.length === expectedKeys.length
    && expectedKeys.every(key => Object.hasOwn(actual, key) && matchesEqual(actual[key], expected[key]));
}

function expectation(actual, negated = false, context = "") {
  const verify = (condition, message) => {
    if (negated ? condition : !condition) {
      const contextualMessage = context ? `${context}: ${message}` : message;
      lastExpectationFailure = contextualMessage;
      throw new Error(contextualMessage);
    }
  };
  const matchers = {
    toEqual(expected) {
      verify(matchesEqual(actual, expected),
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to equal ${describeValue(expected)}`);
    },
    toBe(expected) {
      verify(Object.is(actual, expected),
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to be ${describeValue(expected)}`);
    },
    toBeNull() {
      verify(actual === null, `expected ${describeValue(actual)} ${negated ? "not " : ""}to be null`);
    },
    toBeTrue() {
      verify(actual === true, `expected ${describeValue(actual)} ${negated ? "not " : ""}to be true`);
    },
    toBeFalse() {
      verify(actual === false, `expected ${describeValue(actual)} ${negated ? "not " : ""}to be false`);
    },
    toBeDefined() {
      verify(actual !== undefined,
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to be defined`);
    },
    toBeUndefined() {
      verify(actual === undefined,
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to be undefined`);
    },
    toBeInstanceOf(constructor) {
      verify(actual instanceof constructor,
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to be an instance of ${constructor?.name}`);
    },
    toBeCloseTo(expected, precision = 2) {
      const tolerance = Math.pow(10, -precision) / 2;
      verify(typeof actual === "number" && typeof expected === "number"
          && Math.abs(actual - expected) < tolerance,
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to be close to ${describeValue(expected)}`);
    },
    toHaveClass(className) {
      verify(Boolean(actual?.classList?.contains(className)),
        `expected element ${negated ? "not " : ""}to have class '${className}'${describeElementState(actual)}`);
    },
    toHaveSize(expectedSize) {
      const size = typeof actual?.length === "number" ? actual.length : actual?.size;
      verify(size === expectedSize,
        `expected collection size ${describeValue(size)} ${negated ? "not " : ""}to equal ${expectedSize}`);
    },
    toHaveBeenCalled() {
      verify(Boolean(actual?.calls?.count() > 0),
        `expected spy ${negated ? "not " : ""}to have been called`);
    },
    toHaveBeenCalledWith(...expectedArguments) {
      const matched = actual?.calls?.allArgs().some(argumentsList =>
        argumentsList.length === expectedArguments.length
        && argumentsList.every((value, index) => matchesEqual(value, expectedArguments[index])));
      verify(Boolean(matched),
        `expected spy ${negated ? "not " : ""}to have been called with the supplied arguments`);
    },
    toHaveBeenCalledTimes(expectedCount) {
      const count = actual?.calls?.count();
      verify(count === expectedCount,
        `expected spy call count ${describeValue(count)} ${negated ? "not " : ""}to equal ${expectedCount}`);
    },
    toThrowError(expectedConstructor = Error, expectedMessage) {
      let thrown = null;
      try {
        actual();
      } catch (error) {
        thrown = error;
      }
      const matched = thrown instanceof expectedConstructor
        && (expectedMessage === undefined || thrown.message === expectedMessage);
      verify(matched,
        `expected function ${negated ? "not " : ""}to throw ${expectedConstructor?.name}`
        + (expectedMessage === undefined ? "" : ` with message ${JSON.stringify(expectedMessage)}`));
    },
    toMatch(expected) {
      const matched = expected instanceof RegExp
        ? expected.test(String(actual))
        : String(actual).includes(String(expected));
      verify(matched,
        `expected ${describeValue(actual)} ${negated ? "not " : ""}to match ${describeValue(expected)}`);
    },
    nothing() {
      // Jasmine's explicit no-op assertion documents that reaching this
      // callback is itself the expectation.
    }
  };
  Object.defineProperty(matchers, "not", {
    get: () => expectation(actual, !negated, context)
  });
  matchers.withContext = message => expectation(actual, negated, String(message));
  return matchers;
}

globalThis.expect = actual => expectation(actual);
function createSpyFunction(original = null) {
  const calls = [];
  const spy = function(...args) {
    calls.push(args);
    return spyImplementation?.apply(this, args);
  };
  let spyImplementation = null;
  spy.calls = {
    count: () => calls.length,
    allArgs: () => calls.map(args => args.slice()),
    reset: () => { calls.length = 0; }
  };
  spy.and = {
    callThrough: () => {
      spyImplementation = original;
      return spy;
    },
    returnValue: value => {
      spyImplementation = () => value;
      return spy;
    }
  };
  return spy;
}

globalThis.spyOn = (owner, propertyName) => {
  const original = owner[propertyName];
  if (typeof original !== "function") throw new Error(`'${propertyName}' is not callable`);
  const spy = createSpyFunction(original);
  owner[propertyName] = spy;
  activeSpies.push(() => { owner[propertyName] = original; });
  return spy;
};

globalThis.spyOnProperty = (owner, propertyName, accessType = "get") => {
  if (accessType !== "get" && accessType !== "set") {
    throw new Error(`Unsupported property access type '${accessType}'`);
  }
  const ownDescriptor = Object.getOwnPropertyDescriptor(owner, propertyName);
  let descriptor = ownDescriptor;
  let prototype = Object.getPrototypeOf(owner);
  while (!descriptor && prototype) {
    descriptor = Object.getOwnPropertyDescriptor(prototype, propertyName);
    prototype = Object.getPrototypeOf(prototype);
  }
  const original = descriptor?.[accessType];
  if (typeof original !== "function") {
    throw new Error(`'${propertyName}' has no ${accessType} accessor`);
  }
  const spy = createSpyFunction(original);
  Object.defineProperty(owner, propertyName, {
    configurable: true,
    enumerable: descriptor.enumerable,
    get: accessType === "get" ? spy : descriptor.get,
    set: accessType === "set" ? spy : descriptor.set
  });
  activeSpies.push(() => {
    if (ownDescriptor) Object.defineProperty(owner, propertyName, ownDescriptor);
    else delete owner[propertyName];
  });
  return spy;
};

async function invokeAll(callbacks) {
  for (const callback of callbacks) await callback();
}

async function invokeTest(body, name) {
  const errorStart = state.errors.length;
  lastExpectationFailure = null;
  let timeoutHandle;
  const timeout = new Promise((resolve, reject) => {
    timeoutHandle = setTimeout(
      () => {
        const asynchronousErrors = state.errors.slice(errorStart);
        const asynchronousDetail = asynchronousErrors.length
          ? `; asynchronous error: ${asynchronousErrors[0]}`
          : "";
        const expectationDetail = lastExpectationFailure
          ? `; last failed expectation: ${lastExpectationFailure}`
          : "";
        reject(new Error(
          `Jasmine-compatible test timed out after 5000 ms: ${name}${asynchronousDetail}${expectationDetail}`));
      },
      5000);
  });
  const execution = body.length === 0
    ? Promise.resolve().then(body)
    : new Promise((resolve, reject) => {
      const done = () => resolve();
      done.fail = error => reject(error instanceof Error ? error : new Error(String(error)));
      try {
        const returned = body(done);
        if (returned && typeof returned.then === "function") returned.then(resolve, reject);
      } catch (error) {
        reject(error);
      }
    });
  try {
    await Promise.race([execution, timeout]);
    if (state.errors.length > errorStart) {
      throw new Error(`Jasmine-compatible test raised an asynchronous error: ${state.errors[errorStart]}`);
    }
  } finally {
    clearTimeout(timeoutHandle);
  }
}

function restoreSpies() {
  for (let index = activeSpies.length - 1; index >= 0; index--) activeSpies[index]();
  activeSpies = [];
}

async function executeSuite(suite, ancestors) {
  const lineage = [...ancestors, suite].filter(item => item.name);
  await invokeAll(suite.beforeAll);
  try {
    for (const child of suite.children) {
      if (child.type === "suite") {
        await executeSuite(child.suite, lineage);
        continue;
      }

      const name = [...lineage.map(item => item.name), child.name].join(" ");
      try {
        for (const ancestor of lineage) await invokeAll(ancestor.beforeEach);
        await invokeTest(child.body, name);
        state.results.push({ name, status: "PASS", message: null, stack: null });
      } catch (error) {
        state.results.push({
          name,
          status: "FAIL",
          message: String(error?.message || error),
          stack: error?.stack ? String(error.stack) : null
        });
      } finally {
        try {
          for (let index = lineage.length - 1; index >= 0; index--) {
            await invokeAll(lineage[index].afterEach);
          }
        } catch (error) {
          state.results.push({
            name: `${name} afterEach`,
            status: "FAIL",
            message: String(error?.message || error),
            stack: error?.stack ? String(error.stack) : null
          });
        }
        // Jasmine runs afterEach before advancing its asynchronous queue. Give
        // already-queued component cleanup callbacks a task boundary only after
        // fixture cleanup, but before restoring spies and starting the next spec.
        // This prevents pending transition fallbacks from contaminating the next
        // test without allowing a resolved test's old fixture to restart first.
        const cleanupErrorStart = state.errors.length;
        await new Promise(resolve => setTimeout(resolve, specTaskDrainMilliseconds));
        if (state.errors.length > cleanupErrorStart) {
          state.results.push({
            name: `${name} queued cleanup`,
            status: "FAIL",
            message: String(state.errors[cleanupErrorStart]),
            stack: null
          });
        }
        restoreSpies();
      }
    }
  } finally {
    await invokeAll(suite.afterAll);
  }
}

export async function runRegisteredJasmineTests() {
  try {
    await executeSuite(rootSuite, []);
  } catch (error) {
    state.results.push({
      name: "Jasmine-compatible suite lifecycle",
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
