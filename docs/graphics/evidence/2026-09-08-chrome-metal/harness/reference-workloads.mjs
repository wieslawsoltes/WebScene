// Synthetic drawing data only. Kestrel application files remain byte-for-byte unchanged.
export const referenceSeed = 0x22c0ffee;

export function lineProject(count, seed = referenceSeed) {
  if (!Number.isInteger(count) || count < 1 || count > 200_000) throw new Error("Invalid line count");
  if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) throw new Error("Invalid uint32 seed");
  let state = seed >>> 0;
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return (state >>> 8) / 0x1000000;
  };
  const quantize = value => Math.round(value * 1024) / 1024;
  const entities = Array.from({ length: count }, (_, index) => {
    const x = quantize((random() - 0.5) * 1000);
    const y = quantize((random() - 0.5) * 1000);
    const dx = quantize(2 + random() * 18);
    const dy = quantize((random() - 0.5) * 40);
    return { id: `reference-line-${index}`, type: "LINE", layer: "0", color: "bylayer",
      linetype: "ByLayer", points: [[x, y, 0], [quantize(x + dx), quantize(y + dy), 0]] };
  });
  return { format: "kestrel-cad", version: 1, name: `Seeded ${count} lines`, units: "mm",
    currentLayer: "0", layers: [{ id: "0", name: "REFERENCE", color: "#59c8d9", visible: true,
      locked: false, linetype: "Continuous", lineweight: 0.25 }], entities, camera: null };
}

export const referenceCases = ["courtyard", "fixture", "lines-10000", "lines-100000"].flatMap(scene =>
  [1, 2].flatMap(dpr => ["dark", "light"].map(theme => ({
    id: `${scene}-dpr${dpr}-${theme}`, scene, dpr, theme,
    style: scene === "fixture" ? "shaded-edges" : "wireframe",
    view: scene === "fixture" ? "iso" : "top",
    documentViewport: { width: 1920, height: 1080 }, seed: referenceSeed,
    interaction: { kind: "camera-pan", frames: 180, dxCssPixels: 0.5, dyCssPixels: 0.25 }
  })))
);
