import {
  BoxGeometry,
  Mesh,
  MeshBasicMaterial,
  PerspectiveCamera,
  Scene,
  WebGPURenderer
} from "three/webgpu";

(async () => {
  globalThis.__webSceneThreeWebGpuStage = "construct";
  const canvas = document.createElement("canvas");
  canvas.width = 96;
  canvas.height = 72;
  canvas.style.width = "96px";
  canvas.style.height = "72px";
  document.body.appendChild(canvas);

  const renderer = new WebGPURenderer({ canvas, antialias: false });
  await renderer.init();
  globalThis.__webSceneThreeWebGpuStage = "initialized";
  renderer.setSize(96, 72, false);

  const scene = new Scene();
  const camera = new PerspectiveCamera(60, 4 / 3, 0.1, 10);
  camera.position.z = 2;
  const geometry = new BoxGeometry(1, 1, 1);
  const material = new MeshBasicMaterial({ color: 0x249cff });
  const mesh = new Mesh(geometry, material);
  mesh.rotation.x = 0.35;
  mesh.rotation.y = 0.55;
  scene.add(mesh);

  globalThis.__webSceneThreeWebGpuStage = "render";
  renderer.render(scene, camera);
  globalThis.__webSceneThreeWebGpuComplete = true;
})();
