import { spawn } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { CdpClient } from "../WebPlatformSubset/chrome/cdp-client.mjs";

export const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

export async function evaluate(client, expression) {
  const response = await client.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true }, 60_000);
  if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description ?? response.exceptionDetails.text);
  return response.result.value;
}

export async function startChrome(executable) {
  const userDataDirectory = await mkdtemp(path.join(os.tmpdir(), "webscene-kestrel-chrome-"));
  const args = ["--disable-background-networking", "--disable-component-update", "--disable-default-apps",
    "--disable-extensions", "--disable-sync", "--no-first-run", "--no-default-browser-check",
    "--remote-debugging-port=0", "--window-size=1960,1200", `--user-data-dir=${userDataDirectory}`, "about:blank"];
  const child = spawn(executable, args, { stdio: ["ignore", "ignore", "pipe"] });
  const session = { child, userDataDirectory, args, stderr: "", browser: null, page: null };
  try {
    const endpoint = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("Chrome DevTools startup timed out")), 30_000);
      child.stderr.setEncoding("utf8");
      child.stderr.on("data", chunk => {
        session.stderr += chunk;
        const match = session.stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
        if (match) { clearTimeout(timer); resolve(match[1]); }
      });
      child.once("error", error => { clearTimeout(timer); reject(error); });
      child.once("exit", code => { clearTimeout(timer); reject(new Error(`Chrome exited: ${code}`)); });
    });
    session.browser = await CdpClient.connect(endpoint);
    const url = new URL(endpoint);
    let target;
    for (let attempt = 0; attempt < 100 && !target; ++attempt) {
      const targets = await (await fetch(`http://${url.host}/json/list`)).json();
      target = targets.find(value => value.type === "page" && value.webSocketDebuggerUrl);
      if (!target) await delay(50);
    }
    if (!target) throw new Error("Chrome page target unavailable");
    session.page = await CdpClient.connect(target.webSocketDebuggerUrl);
    await session.page.send("Page.enable");
    await session.page.send("Runtime.enable");
    await session.page.send("Page.bringToFront");
    return session;
  } catch (error) {
    await stopChrome(session);
    throw error;
  }
}

export async function stopChrome(session) {
  try { await session.browser?.send("Browser.close", {}, 2_000); }
  catch { session.child.kill("SIGTERM"); }
  session.page?.close();
  session.browser?.close();
  if (session.child.exitCode === null && session.child.signalCode === null) {
    await new Promise(resolve => {
      const timer = setTimeout(() => { session.child.kill("SIGKILL"); resolve(); }, 2_000);
      session.child.once("exit", () => { clearTimeout(timer); resolve(); });
    });
  }
  await rm(session.userDataDirectory, { recursive: true, force: true });
}

export async function waitFor(client, expression) {
  for (let attempt = 0; attempt < 300; ++attempt) {
    if (await evaluate(client, expression)) return;
    await delay(100);
  }
  throw new Error(`Timed out waiting for ${expression}`);
}
