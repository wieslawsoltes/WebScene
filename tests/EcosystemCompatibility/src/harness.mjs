export const state = globalThis.__webSceneWptState = {
  complete: false,
  harness: null,
  results: [],
  errors: [],
  diagnostics: []
};

globalThis.addEventListener?.("error", event => {
  state.errors.push(String(event?.error?.stack || event?.message || event));
});
globalThis.addEventListener?.("unhandledrejection", event => {
  state.errors.push(String(event?.reason?.stack || event?.reason || event));
});

export function assert(condition, message) {
  if (!condition) throw new Error(message);
}

export function equal(actual, expected, message) {
  if (!Object.is(actual, expected)) {
    throw new Error(`${message}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

export function delay(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}

export async function run(cases) {
  for (const [name, body] of cases) {
    try {
      await body();
      state.results.push({ name, status: "PASS", message: null, stack: null });
    } catch (error) {
      state.results.push({
        name,
        status: "FAIL",
        message: String(error?.message || error),
        stack: error?.stack ? String(error.stack) : null
      });
    }
  }

  const failure = state.results.find(result => result.status !== "PASS");
  const error = state.errors[0];
  state.harness = failure || error
    ? { status: 1, message: failure?.message || error, stack: failure?.stack || null }
    : { status: 0, message: null, stack: null };
  state.complete = true;
}
