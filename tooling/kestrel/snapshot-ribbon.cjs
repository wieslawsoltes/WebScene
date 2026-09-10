// One-time development migration helper. Not used by application builds/runs.
// Run with the pinned upstream checkout and output file as arguments.
const fs = require('node:fs');
const path = require('node:path');
globalThis.Kestrel = {};
require(path.resolve(process.argv[2], 'src/ui.js'));
let output = '<!doctype html><html><body>\n<!-- Original ui.js ribbon output captured as predefined templates. -->\n';
for (const name of Object.keys(Kestrel.UI.groups)) {
  // IDs become instance references, restored when the one active ribbon mounts.
  const markup = Kestrel.UI.ribbon(name).replace(/\bid="([^"]+)"/g, 'data-ref="$1"');
  output += `<template id="ribbon-${name}">${markup}</template>\n`;
}
output += '</body></html>\n';
fs.writeFileSync(process.argv[3], output);
