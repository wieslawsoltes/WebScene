import { mkdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const assetsDirectory = fileURLToPath(new URL("./Assets/", import.meta.url));

await mkdir(assetsDirectory, { recursive: true });

await build({
  entryPoints: {
    monaco: fileURLToPath(new URL("./web/editor-entry.js", import.meta.url))
  },
  outdir: assetsDirectory,
  entryNames: "[name]",
  assetNames: "[name]",
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
