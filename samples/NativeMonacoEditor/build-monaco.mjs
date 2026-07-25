import { mkdir } from "node:fs/promises";
import { build } from "esbuild";

await mkdir(new URL("./Assets/", import.meta.url), { recursive: true });

await build({
  entryPoints: {
    monaco: new URL("./web/editor-entry.js", import.meta.url).pathname
  },
  outdir: new URL("./Assets/", import.meta.url).pathname,
  entryNames: "[name]",
  assetNames: "[name]-[hash]",
  bundle: true,
  format: "iife",
  platform: "browser",
  target: "es2022",
  sourcemap: false,
  minify: false,
  loader: {
    ".ttf": "file"
  }
});
