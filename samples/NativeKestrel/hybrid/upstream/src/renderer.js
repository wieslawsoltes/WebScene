/* Kestrel CAD — retained WebGPU renderer, instanced anti-aliased lines,
 * depth-tested lit meshes, 4x MSAA, camera-relative vertices and Canvas fallback. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M } = K.Math;
    const WGSL = `
 struct Camera { mvp: mat4x4f, eye: vec4f, viewport: vec4f };
 @group(0) @binding(0) var<uniform> camera: Camera;
 struct LineOut { @builtin(position) position: vec4f, @location(0) color: vec4f,
   @location(1) local: vec2f, @location(2) length: f32, @location(3) halfWidth: f32, @location(4) dash: f32 };
 @vertex fn lineVertex(@builtin(vertex_index) vi: u32, @location(0) a: vec3f,
   @location(1) b: vec3f, @location(2) color: vec4f, @location(3) params: vec2f) -> LineOut {
   var pa = camera.mvp * vec4f(a, 1.0); var pb = camera.mvp * vec4f(b, 1.0);
   if (pa.z < 0.0 && pb.z < 0.0) { pa = vec4f(4.0, 4.0, 2.0, 1.0); pb = pa; }
   else if (pa.z < 0.0) { pa = mix(pa, pb, pa.z / (pa.z-pb.z)); }
   else if (pb.z < 0.0) { pb = mix(pb, pa, pb.z / (pb.z-pa.z)); }
   let delta = (pb.xy / pb.w - pa.xy / pa.w) * camera.viewport.xy * 0.5;
   let len = max(length(delta), 0.0001); let dir = delta / len;
   let normal = vec2f(-dir.y, dir.x); let halfWidth = max(params.x * 0.5, 0.25);
   let extent = halfWidth + 1.0;
   let corners = array<vec2f,6>(vec2f(0.,-1.),vec2f(1.,-1.),vec2f(1.,1.),vec2f(0.,-1.),vec2f(1.,1.),vec2f(0.,1.));
   let corner = corners[vi]; let t = corner.x; var p = mix(pa,pb,t);
   let shift = normal*corner.y*extent + dir*(t*2.0-1.0)*extent;
   p = vec4f(p.xy + shift * 2.0 / camera.viewport.xy * p.w,p.z,p.w);
   var o: LineOut; o.position=p; o.color=color; o.local=vec2f(mix(-extent,len+extent,t),corner.y*extent);
   o.length=len; o.halfWidth=halfWidth; o.dash=params.y; return o;
 }
 @fragment fn lineFragment(i: LineOut) -> @location(0) vec4f {
   if (i.dash > 0.5) { let d = i.local.x % 22.0;
     if (i.dash < 1.5 && d > 14.0) { discard; }
     if (i.dash > 1.5 && ((d > 12.0 && d < 16.0) || d > 18.0)) { discard; }
   }
   let outside = max(max(-i.local.x,i.local.x-i.length),0.0);
   let distance = length(vec2f(outside,i.local.y))-i.halfWidth;
   let alpha=clamp(0.5-distance,0.0,1.0)*i.color.a;
   if (alpha < 0.01) { discard; } return vec4f(i.color.rgb,alpha);
 }
 struct MeshOut { @builtin(position) position: vec4f, @location(0) color: vec4f,
   @location(1) normal: vec3f, @location(2) world: vec3f };
 @vertex fn meshVertex(@location(0) p: vec3f,@location(1) normal: vec3f,@location(2) color: vec4f) -> MeshOut {
   var o: MeshOut; o.position=camera.mvp*vec4f(p,1.0);o.color=color;o.normal=normal;o.world=p;return o;
 }
 @fragment fn meshFragment(i: MeshOut,@builtin(front_facing) front: bool) -> @location(0) vec4f {
   var n=normalize(i.normal); if(!front){ n=-n; }
   let light=normalize(vec3f(-0.35,-0.45,0.85));let view=normalize(camera.eye.xyz-i.world);
   let diffuse=max(dot(n,light),0.0);let fill=max(dot(n,normalize(vec3f(0.7,0.2,0.3))),0.0);
   let spec=pow(max(dot(reflect(-light,n),view),0.0),42.0)*0.18;
   let intensity=0.38+diffuse*0.53+fill*0.16;
   let shaded=i.color.rgb*intensity+vec3f(spec);
   return vec4f(mix(i.color.rgb,shaded,camera.viewport.z),i.color.a);
 }`;
    function rgba(hex, alpha = 1) { const n = parseInt((hex || '#dce4ed').slice(1), 16); return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, alpha]; }
    function displayColor(hex, theme) { const c = rgba(hex); if (theme === 'light') {
        const lum = c[0] * .2126 + c[1] * .7152 + c[2] * .0722;
        if (lum > .72)
            return '#263b4e';
        if (lum > .4) {
            return '#' + c.slice(0, 3).map(v => Math.round(v * .72 * 255).toString(16).padStart(2, '0')).join('');
        }
    } return hex; }
    class Renderer {
        constructor(canvas, overlay, camera) { this.canvas = canvas; this.overlay = overlay; this.camera = camera; this.ctx = overlay.getContext('2d'); this.backend = 'Starting'; this.device = null; this.sceneKey = ''; this.gridKey = ''; this.origin = [0, 0, 0]; this.theme = 'dark'; this.style = 'wireframe'; this.grid = true; this.lineweights = false; this.stats = { segments: 0, triangles: 0, cpuMs: 0, drawCalls: 0 }; this.buffers = {}; this.onBackend = () => { }; this.onError = () => { }; this.drawOverlay = () => { }; this.width = 0; this.height = 0; this.gpuErrors = []; this.fallbackReason = ''; }
        async init(forceFallback = false) {
            if (!forceFallback && navigator.gpu) {
                try {
                    this.adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
                    if (!this.adapter)
                        throw Error('No WebGPU adapter is available.');
                    this.device = await this.adapter.requestDevice();
                    const d = this.device;
                    d.pushErrorScope('validation');
                    this.format = navigator.gpu.getPreferredCanvasFormat();
                    const shader = d.createShaderModule({ label: 'Kestrel instanced lines and lit solids', code: WGSL });
                    const compile = await shader.getCompilationInfo();
                    const failures = compile.messages.filter(m => m.type === 'error');
                    if (failures.length)
                        throw Error(failures.map(m => m.message).join('\n'));
                    this.uniform = d.createBuffer({ label: 'Camera uniforms', size: 96, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
                    const layout = d.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } }] });
                    this.bind = d.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer: this.uniform } }] });
                    const pipelineLayout = d.createPipelineLayout({ bindGroupLayouts: [layout] });
                    const blend = { color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' }, alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } };
                    this.linePipeline = d.createRenderPipeline({ label: 'Pixel-width AA instanced line quads', layout: pipelineLayout, vertex: { module: shader, entryPoint: 'lineVertex', buffers: [{ arrayStride: 48, stepMode: 'instance', attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' }, { shaderLocation: 2, offset: 24, format: 'float32x4' }, { shaderLocation: 3, offset: 40, format: 'float32x2' }] }] }, fragment: { module: shader, entryPoint: 'lineFragment', targets: [{ format: this.format, blend }] }, primitive: { topology: 'triangle-list' }, depthStencil: { format: 'depth24plus', depthWriteEnabled: false, depthCompare: 'less-equal', depthBias: -2 }, multisample: { count: 4 } });
                    const meshDesc = { label: 'Depth-tested Lambert mesh triangles', layout: pipelineLayout, vertex: { module: shader, entryPoint: 'meshVertex', buffers: [{ arrayStride: 40, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }, { shaderLocation: 1, offset: 12, format: 'float32x3' }, { shaderLocation: 2, offset: 24, format: 'float32x4' }] }] }, fragment: { module: shader, entryPoint: 'meshFragment', targets: [{ format: this.format, blend }] }, primitive: { topology: 'triangle-list', cullMode: 'none' }, depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' }, multisample: { count: 4 } };
                    this.meshPipeline = d.createRenderPipeline(meshDesc);
                    this.xrayPipeline = d.createRenderPipeline({ ...meshDesc, label: 'Transparent mesh preview', depthStencil: { format: 'depth24plus', depthWriteEnabled: false, depthCompare: 'less-equal' } });
                    const error = await d.popErrorScope();
                    if (error)
                        throw Error(error.message);
                    this.gpuContext = this.canvas.getContext('webgpu');
                    if (!this.gpuContext)
                        throw Error('WebGPU canvas context creation failed.');
                    this.gpuContext.configure({ device: d, format: this.format, alphaMode: 'opaque' });
                    d.addEventListener('uncapturederror', e => { this.gpuErrors.push(e.error.message); this.onError('WebGPU: ' + e.error.message); });
                    d.lost.then(info => { this.fallback('WebGPU device lost: ' + info.message); this.onBackend(this.backend); });
                    this.backend = 'WebGPU';
                    this.adapterName = this.adapter.info?.description || this.adapter.info?.device || 'GPU adapter';
                    this.adapterInfo = this.adapter.info ? { vendor: this.adapter.info.vendor, architecture: this.adapter.info.architecture, device: this.adapter.info.device, description: this.adapter.info.description } : null;
                }
                catch (error) {
                    this.fallback(error.message);
                }
            }
            else
                this.fallback(forceFallback ? 'Canvas fallback requested.' : !window.isSecureContext ? 'WebGPU requires localhost or HTTPS.' : 'WebGPU is unavailable in this browser.');
            this.onBackend(this.backend);
            return this.backend;
        }
        fallback(reason) { this.backend = 'Canvas 2D'; this.fallbackReason = reason; this.device = null; this.sceneKey = ''; if (!this.fallbackCanvas) {
            this.fallbackCanvas = document.createElement('canvas');
            this.fallbackCanvas.className = 'scene-canvas';
            this.fallbackCanvas.setAttribute('aria-hidden', 'true');
            this.canvas.parentNode.insertBefore(this.fallbackCanvas, this.overlay);
            this.fallbackContext = this.fallbackCanvas.getContext('2d');
        } this.canvas.style.display = 'none'; this.resize(this.width || 800, this.height || 600, true); }
        resize(w, h, force = false) { const dpr = Math.min(window.devicePixelRatio || 1, 2); w = Math.max(1, Math.round(w)); h = Math.max(1, Math.round(h)); if (!force && w === this.width && h === this.height && dpr === this.dpr)
            return; this.width = w; this.height = h; this.dpr = dpr; const pw = Math.round(w * dpr), ph = Math.round(h * dpr); for (const c of [this.canvas, this.overlay, this.fallbackCanvas].filter(Boolean)) {
            c.width = pw;
            c.height = ph;
            c.style.width = w + 'px';
            c.style.height = h + 'px';
        } this.camera.resize(w, h); if (this.device) {
            this.depth?.destroy();
            this.msaa?.destroy();
            this.depth = this.device.createTexture({ label: 'Viewport depth', size: [pw, ph], sampleCount: 4, format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT });
            this.msaa = this.device.createTexture({ label: '4x multisample viewport', size: [pw, ph], sampleCount: 4, format: this.format, usage: GPUTextureUsage.RENDER_ATTACHMENT });
        } this.sceneKey = ''; this.gridKey = ''; this.canvasFrameKey = ''; }
        upload(name, array, stride) { if (!this.device)
            return; const size = Math.max(4, array.byteLength), entry = this.buffers[name]; if (!entry || entry.size < size) {
            entry?.buffer.destroy();
            const capacity = 2 ** Math.ceil(Math.log2(Math.max(256, size)));
            this.buffers[name] = { buffer: this.device.createBuffer({ label: name, size: capacity, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }), size: capacity, count: array.length / stride };
        }
        else
            entry.count = array.length / stride; if (array.length)
            this.device.queue.writeBuffer(this.buffers[name].buffer, 0, array); }
        buildScene(doc) {
            const key = [doc.id, doc.revision, [...doc.selection].sort().join(','), this.style, this.theme, this.lineweights].join(':');
            if (key === this.sceneKey)
                return;
            this.sceneKey = key;
            this.origin = this.camera.target.slice();
            this.gridKey = '';
            const lines = [], triangles = [];
            this.texts = [];
            this.fallbackLines = [];
            this.fallbackTriangles = [];
            const addLine = (s, color, width, dash) => { lines.push(...V.sub(s[0], this.origin), ...V.sub(s[1], this.origin), ...color, width, dash); this.fallbackLines.push({ s, color, width, dash }); };
            let triCount = 0, entityCount = 0;
            for (const {e, owner} of (K.Production?.renderEntities ? K.Production.renderEntities(doc) : doc.entities.map(e=>({e,owner:e.id})))) {
                if (!doc.visible(e))
                    continue;
                entityCount++;
                const g = doc.geometry(e), layer = doc.layer(e), selected = doc.selection.has(owner), locked = layer.locked;
                const raw = e.color && !['bylayer','byblock'].includes(e.color) ? e.color : layer.color, col = displayColor(raw, this.theme);
                let color = rgba(selected ? '#63d7eb' : col, locked ? .42 : 1);
                const isMesh = e.type === 'MESH', linetype = e.linetype === 'ByLayer' || !e.linetype ? layer.linetype : e.linetype, dash = /center/i.test(linetype) ? 2 : /dash/i.test(linetype) ? 1 : 0, width = selected ? 2.1 : this.lineweights ? Math.max(.8, (e.lineweight || layer.lineweight || .25) * 4) : isMesh ? .75 : 1.05;
                if (!(isMesh && this.style === 'shaded')) {
                    const edgeColor = isMesh && this.style !== 'wireframe' && !selected ? rgba(this.theme === 'dark' ? '#13222e' : '#314958', .62) : color;
                    for (const s of (isMesh && this.style === 'wireframe' ? g.wireSegments || g.segments : g.segments))
                        addLine(s, edgeColor, width, dash);
                }
                if (!isMesh || this.style !== 'wireframe') {
                    const fillColor = color.slice();
                    if (this.style === 'xray' && isMesh)
                        fillColor[3] = .25;
                    for (const t of g.triangles) {
                        for (let j = 0; j < 3; j++)
                            triangles.push(...V.sub(t.points[j], this.origin), ...(t.normals?.[j] || t.normal), ...fillColor);
                        this.fallbackTriangles.push({ points: t.points, normal: t.normal, color: fillColor });
                        triCount++;
                    }
                }
                for (const t of g.texts)
                    this.texts.push({ t, color: selected ? '#63d7eb' : col, alpha: locked ? .4 : 1, dimension: e.type === 'DIMENSION', selected });
            }
            this.stats.segments = lines.length / 12;
            this.stats.triangles = triCount;
            this.stats.entities = entityCount;
            this.upload('scene-lines', new Float32Array(lines), 12);
            this.upload('scene-triangles', new Float32Array(triangles), 10);
        }
        gridSpacing() { const zoom = this.camera.zoom; let s = 10 ** Math.floor(Math.log10(28 / Math.max(zoom, 1e-9))); if (s * zoom < 14)
            s *= 2; if (s * zoom < 14)
            s *= 2.5; return s; }
        buildGrid() {
            const key = this.camera.revision + ':' + this.grid + ':' + this.theme;
            if (this.gridKey === key)
                return;
            this.gridKey = key;
            this.gridLines = [];
            const arr = [], s = this.gridSpacing();
            this.spacing = s;
            if (this.grid) {
                let cx = this.camera.target[0], cy = this.camera.target[1], range = Math.max(this.width, this.height) / this.camera.zoom * .95;
                range = Math.min(range, s * 90);
                const n = Math.ceil(range / s), x0 = Math.floor(cx / s) * s, y0 = Math.floor(cy / s) * s;
                const minor = rgba(this.theme === 'dark' ? '#253446' : '#d9e0e7', .7), major = rgba(this.theme === 'dark' ? '#34485c' : '#c7d1db', .8);
                const add = (a, b, color, width = .55) => { arr.push(...V.sub(a, this.origin), ...V.sub(b, this.origin), ...color, width, 0); this.gridLines.push({ s: [a, b], color, width, dash: 0 }); };
                for (let i = -n; i <= n; i++) {
                    const x = x0 + i * s, y = y0 + i * s;
                    add([x, y0 - range, -.02], [x, y0 + range, -.02], Math.round(x / s) % 5 === 0 ? major : minor);
                    add([x0 - range, y, -.02], [x0 + range, y, -.02], Math.round(y / s) % 5 === 0 ? major : minor);
                }
                if (Math.abs(cx) < range)
                    add([0, y0 - range, 0], [0, y0 + range, 0], rgba('#52947d', .55), .9);
                if (Math.abs(cy) < range)
                    add([x0 - range, 0, 0], [x0 + range, 0, 0], rgba('#a16169', .55), .9);
            }
            this.upload('grid-lines', new Float32Array(arr), 12);
        }
        render(doc) { const start = performance.now(); if (!this.width || !this.height)
            return; this.buildScene(doc); this.buildGrid(); if (this.device)
            this.drawGPU();
        else {
            const key = this.sceneKey + ':' + this.gridKey + ':' + this.camera.revision;
            if (this.canvasFrameKey !== key) {
                this.drawCanvas();
                this.canvasFrameKey = key;
            }
        } this.drawAnnotations(); this.drawOverlay(this.ctx); this.stats.cpuMs = performance.now() - start; }
        drawGPU() {
            const d = this.device, relative = M.multiply(this.camera.matrix, M.translation(...this.origin)), u = new Float32Array(24);
            u.set(relative);
            u.set([...V.sub(this.camera.eye, this.origin), 1], 16);
            u.set([this.width, this.height, this.style === 'wireframe' ? 0 : 1, this.theme === 'light' ? 1 : 0], 20);
            d.queue.writeBuffer(this.uniform, 0, u);
            const encoder = d.createCommandEncoder({ label: 'Kestrel viewport frame' }), bg = rgba(this.theme === 'light' ? '#edf2f6' : '#121c29');
            const pass = encoder.beginRenderPass({ colorAttachments: [{ view: this.msaa.createView(), resolveTarget: this.gpuContext.getCurrentTexture().createView(), clearValue: { r: bg[0], g: bg[1], b: bg[2], a: 1 }, loadOp: 'clear', storeOp: 'store' }], depthStencilAttachment: { view: this.depth.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' } });
            pass.setBindGroup(0, this.bind);
            let calls = 0;
            const lines = name => { const b = this.buffers[name]; if (b?.count) {
                pass.setPipeline(this.linePipeline);
                pass.setVertexBuffer(0, b.buffer);
                pass.draw(6, b.count);
                calls++;
            } };
            lines('grid-lines');
            const tri = this.buffers['scene-triangles'];
            if (tri?.count) {
                pass.setPipeline(this.style === 'xray' ? this.xrayPipeline : this.meshPipeline);
                pass.setVertexBuffer(0, tri.buffer);
                pass.draw(tri.count);
                calls++;
            }
            lines('scene-lines');
            pass.end();
            d.queue.submit([encoder.finish()]);
            this.stats.drawCalls = calls;
        }
        drawCanvas() {
            const c = this.fallbackContext;
            if (!c)
                return;
            c.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
            c.fillStyle = this.theme === 'light' ? '#edf2f6' : '#121c29';
            c.fillRect(0, 0, this.width, this.height);
            const css = a => `rgba(${Math.round(a[0] * 255)},${Math.round(a[1] * 255)},${Math.round(a[2] * 255)},${a[3]})`;
            const projectLine = l => {
                const a = this.camera.project(l.s[0]), b = this.camera.project(l.s[1]);
                if (a[2] < 0 || b[2] < 0 || (a[2] > 1 && b[2] > 1))
                    return null;
                // Liang–Barsky viewport clipping also bounds the occlusion sampling cost.
                let lo = 0, hi = 1;
                const dx = b[0] - a[0], dy = b[1] - a[1], p = [-dx, dx, -dy, dy], q = [a[0] + 4, this.width + 4 - a[0], a[1] + 4, this.height + 4 - a[1]];
                for (let i = 0; i < 4; i++) {
                    if (Math.abs(p[i]) < 1e-12) {
                        if (q[i] < 0)
                            return null;
                    }
                    else {
                        const t = q[i] / p[i];
                        if (p[i] < 0)
                            lo = Math.max(lo, t);
                        else
                            hi = Math.min(hi, t);
                        if (lo > hi)
                            return null;
                    }
                }
                return [V.lerp(a, b, lo), V.lerp(a, b, hi)];
            };
            const drawLine = (l, depth = null) => {
                const projected = projectLine(l);
                if (!projected)
                    return;
                const [a, b] = projected;
                c.strokeStyle = css(l.color);
                c.lineWidth = l.width;
                c.setLineDash(l.dash === 1 ? [14, 8] : l.dash === 2 ? [12, 4, 2, 4] : []);
                c.beginPath();
                if (!depth) {
                    c.moveTo(a[0], a[1]);
                    c.lineTo(b[0], b[1]);
                }
                else {
                    const n = Math.max(1, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / 1.6));
                    let open = false;
                    for (let i = 0; i <= n; i++) {
                        const t = i / n, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
                        const ix = Math.floor(x * depth.scale), iy = Math.floor(y * depth.scale), visible = ix < 0 || iy < 0 || ix >= depth.w || iy >= depth.h || z <= depth.values[iy * depth.w + ix] + depth.epsilon;
                        if (visible) {
                            if (!open)
                                c.moveTo(x, y);
                            else
                                c.lineTo(x, y);
                            open = true;
                        }
                        else
                            open = false;
                    }
                }
                c.stroke();
            };
            for (const l of this.gridLines)
                drawLine(l);
            const triangles = this.fallbackTriangles.map(t => ({ ...t, p: t.points.map(p => this.camera.project(p)) })).filter(t => t.p.every(p => p[2] >= 0 && p[2] <= 1)).sort((a, b) => b.p.reduce((s, p) => s + p[2], 0) - a.p.reduce((s, p) => s + p[2], 0));
            let depth = null, image = null;
            if (triangles.length && this.style !== 'wireframe' && this.style !== 'xray') {
                // A full-resolution software depth/color pass avoids painter-order
                // artifacts where independently triangulated CAD faces overlap.
                const w = this.fallbackCanvas.width, h = this.fallbackCanvas.height;
                if (!this.cpuDepth || this.cpuDepth.length !== w * h)
                    this.cpuDepth = new Float32Array(w * h);
                this.cpuDepth.fill(Infinity);
                depth = { w, h, scale: this.dpr, values: this.cpuDepth,
                    epsilon: this.camera.perspective ? 2e-7 : .9 / (this.camera.zoom * this.camera.distance * 100) };
                image = c.getImageData(0, 0, w, h);
            }
            c.setLineDash([]);
            const keyLight = V.norm([-.35, -.45, .85]), fillLight = V.norm([.7, .2, .3]);
            for (const t of triangles) {
                const center = V.mul(t.points.reduce((v, p) => V.add(v, p), [0, 0, 0]), 1 / 3),
                    normal = V.dot(t.normal, V.sub(this.camera.eye, center)) < 0 ? V.mul(t.normal, -1) : t.normal;
                const f = this.style === 'wireframe' ? 1 : .38 + .53 * Math.max(0, V.dot(normal, keyLight)) + .16 * Math.max(0, V.dot(normal, fillLight));
                const color = t.color.map((v, i) => i < 3 ? v * f : v);
                if (!depth) {
                    c.fillStyle = css(color);
                    c.beginPath();
                    t.p.forEach((p, i) => i ? c.lineTo(p[0], p[1]) : c.moveTo(p[0], p[1]));
                    c.closePath(); c.fill();
                    continue;
                }
                const [a, b, d] = t.p.map(p => [p[0] * depth.scale, p[1] * depth.scale, p[2]]),
                    den = (b[1] - d[1]) * (a[0] - d[0]) + (d[0] - b[0]) * (a[1] - d[1]);
                if (Math.abs(den) < 1e-10) continue;
                const x0 = Math.max(0, Math.floor(Math.min(a[0], b[0], d[0]))),
                    x1 = Math.min(depth.w - 1, Math.ceil(Math.max(a[0], b[0], d[0]))),
                    y0 = Math.max(0, Math.floor(Math.min(a[1], b[1], d[1]))),
                    y1 = Math.min(depth.h - 1, Math.ceil(Math.max(a[1], b[1], d[1])));
                const dUx = (b[1] - d[1]) / den, dVx = (d[1] - a[1]) / den,
                    alpha = color[3], red = Math.min(255, color[0] * 255),
                    green = Math.min(255, color[1] * 255), blue = Math.min(255, color[2] * 255), data = image.data;
                for (let y = y0; y <= y1; y++) {
                    let u = ((b[1] - d[1]) * (x0 + .5 - d[0]) + (d[0] - b[0]) * (y + .5 - d[1])) / den,
                        v = ((d[1] - a[1]) * (x0 + .5 - d[0]) + (a[0] - d[0]) * (y + .5 - d[1])) / den;
                    for (let x = x0; x <= x1; x++, u += dUx, v += dVx) {
                        if (u < -1e-7 || v < -1e-7 || u + v > 1.0000001) continue;
                        const z = u * a[2] + v * b[2] + (1 - u - v) * d[2], idx = y * depth.w + x;
                        if (z >= depth.values[idx]) continue;
                        depth.values[idx] = z;
                        const pixel = idx * 4;
                        data[pixel] = red * alpha + data[pixel] * (1 - alpha);
                        data[pixel + 1] = green * alpha + data[pixel + 1] * (1 - alpha);
                        data[pixel + 2] = blue * alpha + data[pixel + 2] * (1 - alpha);
                        data[pixel + 3] = 255;
                    }
                }
            }
            if (image) c.putImageData(image, 0, 0);
            for (const l of this.fallbackLines)
                drawLine(l, depth);
            c.setLineDash([]);
            this.stats.drawCalls = 0;
        }
        drawAnnotations() {
            const c = this.ctx;
            c.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
            c.clearRect(0, 0, this.width, this.height);
            c.lineJoin = 'round';
            c.lineCap = 'round';
            for (const { t, color, alpha, dimension, selected } of this.texts) {
                if(t.composition && K.MText){K.MText.draw(c,t,p=>this.camera.project(p),color,alpha,this.theme,selected);continue;}
                if(K.Fonts && t.fontFamily){K.Fonts.drawText(c,t,p=>this.camera.project(p),color,alpha);continue;}
                const p = this.camera.project(t.position);
                if (p[2] < 0 || p[2] > 1 || p[0] < -500 || p[0] > this.width + 500 || p[1] < -200 || p[1] > this.height + 200)
                    continue;
                const axes = K.Geo.textAxes(t), h = t.height || 10, q = this.camera.project(V.add(t.position, V.mul(axes.x, h))), r = this.camera.project(V.add(t.position, V.mul(axes.y, h)));
                const px = Math.hypot(q[0] - p[0], q[1] - p[1]);
                if (px < 2 || px > 2000)
                    continue;
                let xx = (q[0] - p[0]) / px, xy = (q[1] - p[1]) / px, yx = -(r[0] - p[0]) / px, yy = -(r[1] - p[1]) / px;
                if (Math.abs(xx * yy - yx * xy) < .015)
                    continue;
                if (dimension && xx < 0) {
                    xx = -xx;
                    xy = -xy;
                    yx = -yx;
                    yy = -yy;
                }
                c.save();
                c.translate(p[0], p[1]);
                c.transform(xx, xy, yx, yy, 0, 0);
                c.font = `${px}px "Segoe UI", Arial, sans-serif`;
                c.textAlign = t.align || 'left';
                c.textBaseline = 'alphabetic';
                c.globalAlpha = alpha;
                c.fillStyle = color;
                const lines = (t.text || '').split('\n');
                lines.forEach((line, i) => { if (dimension) {
                    const m = c.measureText(line), x = t.align === 'center' ? -m.width / 2 : t.align === 'right' ? -m.width : 0;
                    c.save();
                    c.fillStyle = this.theme === 'light' ? '#edf2f6' : '#121c29';
                    c.fillRect(x - px * .2, i * px * 1.35 - px * .85, m.width + px * .4, px * 1.2);
                    c.restore();
                } c.fillText(line, 0, i * px * 1.35); });
                c.restore();
            }
            this.drawUCS(c);
        }
        drawUCS(c) { const x = 40, y = this.height - 37, cam = this.camera, axes = [[[1, 0, 0], 'X', '#d17f88'], [[0, 1, 0], 'Y', '#79b89e'], [[0, 0, 1], 'Z', '#7eabdf']]; c.save(); c.lineWidth = 1.7; c.font = '10px "Segoe UI",sans-serif'; for (const [v, label, color] of axes) {
            const dx = V.dot(v, cam.right) * 28, dy = -V.dot(v, cam.up) * 28;
            if (Math.hypot(dx, dy) < 3)
                continue;
            c.strokeStyle = color;
            c.fillStyle = color;
            c.beginPath();
            c.moveTo(x, y);
            c.lineTo(x + dx, y + dy);
            c.stroke();
            c.fillText(label, x + dx + 4, y + dy + 3);
        } c.fillStyle = this.theme === 'light' ? '#748595' : '#7c90a6'; c.font = '9px "Segoe UI",sans-serif'; c.fillText('WCS', x - 10, y + 20); c.restore(); }
        screenshot() { const out = document.createElement('canvas'); out.width = this.overlay.width; out.height = this.overlay.height; const c = out.getContext('2d'); c.drawImage(this.device ? this.canvas : this.fallbackCanvas, 0, 0); c.drawImage(this.overlay, 0, 0); return out; }
        destroy() { for (const b of Object.values(this.buffers))
            b.buffer.destroy(); this.depth?.destroy(); this.msaa?.destroy(); this.device?.destroy(); }
    }
    K.Renderer = Renderer;
    K.displayColor = displayColor;
    K.WGSL = WGSL;
})(typeof window !== 'undefined' ? window : globalThis);
