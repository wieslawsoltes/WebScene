#!/usr/bin/env node
import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const root = path.dirname(fileURLToPath(import.meta.url));
const outputRoot = path.join(root, "contracts");
const jqueryTestsuiteCss = await readFile(
  path.join(root, "upstream/jquery/test/data/testsuite.css"), "utf8");
const qunitFixtureCss = `
#qunit-fixture {
  position: absolute;
  top: -10000px;
  left: -10000px;
  width: 1000px;
  height: 1000px;
}`;
const bootstrapUpstreamSourceResolver = {
  name: "bootstrap-upstream-source",
  setup(buildContext) {
    buildContext.onResolve({ filter: /^\.\.\/\.\.\/src\// }, args => {
      if (!args.importer.includes("upstream/bootstrap/js/tests/unit/")) return null;
      const sourcePath = args.path.slice("../../src/".length);
      return { path: path.join(root, "node_modules/bootstrap/js/src", sourcePath) };
    });
  }
};
const cases = [
  { id: "jquery-core", entry: "src/jquery-core.mjs", title: "jQuery core composition", css: "" },
  {
    id: "jquery-callbacks-upstream",
    entry: "src/jquery-callbacks-upstream.mjs",
    title: "jQuery upstream callbacks suite",
    css: ""
  },
  {
    id: "jquery-basic-upstream",
    entry: "src/jquery-basic-upstream.mjs",
    title: "jQuery upstream basic integration suite",
    css: ""
  },
  ...Array.from({ length: 2 }, (_, index) => ({
    id: `jquery-core-upstream-${index + 1}`,
    entry: "src/jquery-core-upstream.mjs",
    title: `jQuery upstream core suite shard ${index + 1}/2`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:2};globalThis.__webSceneQUnitExcludedNames=["jQuery('massive html trac-7990')"];</script>`,
    css: ""
  })),
  {
    id: "jquery-core-upstream-massive-html",
    entry: "src/jquery-core-upstream.mjs",
    title: "jQuery upstream core massive HTML case",
    preamble: `<script>globalThis.__webSceneQUnitOnlyNames=["jQuery('massive html trac-7990')"];</script>`,
    css: ""
  },
  ...Array.from({ length: 4 }, (_, index) => ({
    id: `jquery-selector-upstream-${index + 1}`,
    entry: "src/jquery-selector-upstream.mjs",
    title: `jQuery upstream selector suite shard ${index + 1}/4`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:4};</script>`,
    css: ""
  })),
  ...Array.from({ length: 3 }, (_, index) => ({
    id: `jquery-attributes-upstream-${index + 1}`,
    entry: "src/jquery-attributes-upstream.mjs",
    title: `jQuery upstream attributes suite shard ${index + 1}/3`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:3};</script>`,
    css: ""
  })),
  ...Array.from({ length: 4 }, (_, index) => ({
    id: `jquery-css-upstream-${index + 1}`,
    entry: "src/jquery-css-upstream.mjs",
    title: `jQuery upstream CSS suite shard ${index + 1}/4`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:4};</script>`,
    css: `${qunitFixtureCss}\n${jqueryTestsuiteCss}`
  })),
  {
    id: "jquery-serialize-upstream",
    entry: "src/jquery-serialize-upstream.mjs",
    title: "jQuery upstream serialize suite",
    css: ""
  },
  ...Array.from({ length: 4 }, (_, index) => ({
    id: `jquery-traversing-upstream-${index + 1}`,
    entry: "src/jquery-traversing-upstream.mjs",
    title: `jQuery upstream traversing suite ${index + 1}/4`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:4};</script>`,
    css: ""
  })),
  ...Array.from({ length: 4 }, (_, index) => ({
    id: `jquery-dimensions-upstream-${index + 1}`,
    entry: "src/jquery-dimensions-upstream.mjs",
    title: `jQuery upstream dimensions suite ${index + 1}/4`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:4};</script>`,
    css: `${qunitFixtureCss}\n${jqueryTestsuiteCss}`
  })),
  {
    id: "jquery-deferred-upstream",
    entry: "src/jquery-deferred-upstream.mjs",
    title: "jQuery upstream Deferred suite",
    css: ""
  },
  {
    id: "jquery-deprecated-upstream",
    entry: "src/jquery-deprecated-upstream.mjs",
    title: "jQuery upstream deprecated APIs suite",
    css: ""
  },
  ...Array.from({ length: 3 }, (_, index) => ({
    id: `jquery-data-upstream-${index + 1}`,
    entry: "src/jquery-data-upstream.mjs",
    title: `jQuery upstream data suite ${index + 1}/3`,
    preamble: `<script>globalThis.__webSceneQUnitShard={index:${index},count:3};</script>`,
    css: ""
  })),
  {
    id: "jquery-queue-upstream",
    entry: "src/jquery-queue-upstream.mjs",
    title: "jQuery upstream queue suite",
    css: ""
  },
  {
    id: "jquery-ready-upstream",
    entry: "src/jquery-ready-upstream.mjs",
    title: "jQuery upstream DOM ready suite",
    css: ""
  },
  {
    id: "jquery-wrap-upstream",
    entry: "src/jquery-wrap-upstream.mjs",
    title: "jQuery upstream wrapping suite",
    css: ""
  },
  {
    id: "jquery-offset-upstream",
    entry: "src/jquery-offset-upstream.mjs",
    title: "jQuery upstream offset suite",
    css: ""
  },
  {
    id: "bootstrap-components",
    entry: "src/bootstrap-components.mjs",
    title: "Bootstrap component composition",
    css: await readFile(path.join(root, "node_modules/bootstrap/dist/css/bootstrap.css"), "utf8")
  },
  {
    id: "bootstrap-alert-upstream",
    entry: "src/bootstrap-alert-upstream.mjs",
    title: "Bootstrap upstream alert suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-base-component-upstream",
    entry: "src/bootstrap-base-component-upstream.mjs",
    title: "Bootstrap upstream base component suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-button-upstream",
    entry: "src/bootstrap-button-upstream.mjs",
    title: "Bootstrap upstream button suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-collapse-upstream",
    entry: "src/bootstrap-collapse-upstream.mjs",
    title: "Bootstrap upstream collapse suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-toast-upstream",
    entry: "src/bootstrap-toast-upstream.mjs",
    title: "Bootstrap upstream toast suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-tab-upstream",
    entry: "src/bootstrap-tab-upstream.mjs",
    title: "Bootstrap upstream tab suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-dropdown-upstream",
    entry: "src/bootstrap-dropdown-upstream.mjs",
    title: "Bootstrap upstream dropdown suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-modal-upstream",
    entry: "src/bootstrap-modal-upstream.mjs",
    title: "Bootstrap upstream modal suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-offcanvas-upstream",
    entry: "src/bootstrap-offcanvas-upstream.mjs",
    title: "Bootstrap upstream offcanvas suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-carousel-upstream",
    entry: "src/bootstrap-carousel-upstream.mjs",
    title: "Bootstrap upstream carousel suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-scrollspy-upstream",
    entry: "src/bootstrap-scrollspy-upstream.mjs",
    title: "Bootstrap upstream ScrollSpy suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-jquery-upstream",
    entry: "src/bootstrap-jquery-upstream.mjs",
    title: "Bootstrap upstream jQuery integration suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-popover-upstream",
    entry: "src/bootstrap-popover-upstream.mjs",
    title: "Bootstrap upstream Popover suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  {
    id: "bootstrap-tooltip-upstream",
    entry: "src/bootstrap-tooltip-upstream.mjs",
    title: "Bootstrap upstream Tooltip suite",
    css: "",
    plugins: [bootstrapUpstreamSourceResolver]
  },
  { id: "react-dom", entry: "src/react-dom.mjs", title: "React DOM composition", css: "" }
];

await mkdir(outputRoot, { recursive: true });
const files = {};
for (const item of cases) {
  const result = await build({
    entryPoints: [path.join(root, item.entry)],
    bundle: true,
    write: false,
    format: "iife",
    platform: "browser",
    target: ["es2022"],
    loader: { ".html": "text" },
    plugins: item.plugins ?? [],
    logLevel: "silent",
    sourcemap: false,
    legalComments: "none"
  });
  const script = result.outputFiles[0].text.replaceAll("</script", "<\\/script");
  const style = item.css ? `<style>${item.css.replaceAll("</style", "<\\/style")}</style>` : "";
  const html = `<!doctype html>\n<meta charset="utf-8">\n<title>${item.title}</title>\n${style}\n<body><div id="fixture-root"></div><div id="portal-root"></div>${item.preamble ?? ""}<script>${script}</script></body>\n`;
  const file = path.join(outputRoot, `${item.id}.html`);
  await writeFile(file, html);
  files[`contracts/${item.id}.html`] = createHash("sha256").update(html).digest("hex");
}

const lockBytes = await readFile(path.join(root, "package-lock.json"));
const upstreamSourceManifestBytes = await readFile(path.join(root, "upstream-sources.json"));
const upstreamSourceManifest = JSON.parse(upstreamSourceManifestBytes);
const upstreamFiles = {};
for (const suite of upstreamSourceManifest.suites) {
  for (const source of [...suite.selected, ...suite.supportFiles]) {
    const bytes = await readFile(path.join(root, source.path));
    const digest = createHash("sha256").update(bytes).digest("hex");
    if (digest !== source.sha256) {
      throw new Error(`${source.path} differs from pinned ${suite.tag} source (${digest} != ${source.sha256}).`);
    }
    upstreamFiles[source.path] = digest;
  }
}
await writeFile(path.join(outputRoot, "provenance.json"), JSON.stringify({
  schema: "webscene-ecosystem-contract-provenance-v1",
  packageLockSha256: createHash("sha256").update(lockBytes).digest("hex"),
  upstreamSourceManifestSha256: createHash("sha256").update(upstreamSourceManifestBytes).digest("hex"),
  upstreamFiles,
  files
}, null, 2) + "\n");
