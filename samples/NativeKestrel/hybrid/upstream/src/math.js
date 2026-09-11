/* Kestrel CAD — numerical utilities. No runtime dependencies. */
(function (root) {
    'use strict';
    const K = root.Kestrel = root.Kestrel || {};
    const EPS = 1e-8, TAU = Math.PI * 2;
    const V = {
        add: (a, b) => [a[0] + b[0], a[1] + b[1], (a[2] || 0) + (b[2] || 0)],
        sub: (a, b) => [a[0] - b[0], a[1] - b[1], (a[2] || 0) - (b[2] || 0)],
        mul: (a, s) => [a[0] * s, a[1] * s, (a[2] || 0) * s],
        dot: (a, b) => a[0] * b[0] + a[1] * b[1] + (a[2] || 0) * (b[2] || 0),
        cross: (a, b) => [a[1] * (b[2] || 0) - (a[2] || 0) * b[1], (a[2] || 0) * b[0] - a[0] * (b[2] || 0), a[0] * b[1] - a[1] * b[0]],
        len: a => Math.hypot(...a),
        dist: (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], (a[2] || 0) - (b[2] || 0)),
        norm(a) { const l = V.len(a); return l > EPS ? V.mul(a, 1 / l) : [0, 0, 0]; },
        lerp: (a, b, t) => V.add(a, V.mul(V.sub(b, a), t)),
        same: (a, b, eps = EPS) => V.dist(a, b) < eps
    };
    const M = {
        identity: () => new Float64Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
        multiply(a, b) { const r = new Float64Array(16); for (let c = 0; c < 4; c++)
            for (let row = 0; row < 4; row++)
                for (let k = 0; k < 4; k++)
                    r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k]; return r; },
        point(m, p, w = 1) { const x = p[0], y = p[1], z = p[2] || 0; const q = [m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w, m[2] * x + m[6] * y + m[10] * z + m[14] * w, m[3] * x + m[7] * y + m[11] * z + m[15] * w]; if (w && Math.abs(q[3]) > EPS)
            return q.slice(0, 3).map(v => v / q[3]); return q.slice(0, 3); },
        translation(x, y, z = 0) { const m = M.identity(); m[12] = x; m[13] = y; m[14] = z; return m; },
        scale(sx, sy = sx, sz = sx) { const m = M.identity(); m[0] = sx; m[5] = sy; m[10] = sz; return m; },
        rotation(angle, axis = [0, 0, 1]) { const [x, y, z] = V.norm(axis), c = Math.cos(angle), s = Math.sin(angle), t = 1 - c; return new Float64Array([t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0, t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0, t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0, 0, 0, 0, 1]); },
        around(p, m) { return M.multiply(M.translation(...p), M.multiply(m, M.translation(...V.mul(p, -1)))); },
        inverse(a) { const aug = Array.from({ length: 4 }, (_, r) => [...Array.from({ length: 4 }, (_, c) => a[c * 4 + r]), ...Array.from({ length: 4 }, (_, c) => r === c ? 1 : 0)]); for (let i = 0; i < 4; i++) {
            let k = i;
            for (let r = i + 1; r < 4; r++)
                if (Math.abs(aug[r][i]) > Math.abs(aug[k][i]))
                    k = r;
            if (Math.abs(aug[k][i]) < 1e-16)
                return null;
            [aug[i], aug[k]] = [aug[k], aug[i]];
            const s = aug[i][i];
            aug[i] = aug[i].map(v => v / s);
            for (let r = 0; r < 4; r++)
                if (r !== i) {
                    const f = aug[r][i];
                    for (let c = 0; c < 8; c++)
                        aug[r][c] -= f * aug[i][c];
                }
        } const out = new Float64Array(16); for (let r = 0; r < 4; r++)
            for (let c = 0; c < 4; c++)
                out[c * 4 + r] = aug[r][c + 4]; return out; },
        ortho(l, r, b, t, n, f) { const m = M.identity(); m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = 1 / (n - f); m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = n / (n - f); return m; },
        perspective(fov, aspect, n, f) { const t = 1 / Math.tan(fov / 2), m = new Float64Array(16); m[0] = t / aspect; m[5] = t; m[10] = f / (n - f); m[11] = -1; m[14] = n * f / (n - f); return m; }
    };
    function basis(normal = [0, 0, 1]) { const n = V.norm(normal); const x = V.norm(V.cross(Math.abs(n[0]) < 1 / 64 && Math.abs(n[1]) < 1 / 64 ? [0, 1, 0] : [0, 0, 1], n)); return { x, y: V.cross(n, x), n }; }
    function angle(a) { return ((a % TAU) + TAU) % TAU; }
    function sweep(a, b) { let s = angle(b - a); return s < EPS ? TAU : s; }
    function lineIntersection(a, b, c, d) { const r = V.sub(b, a), s = V.sub(d, c), den = r[0] * s[1] - r[1] * s[0]; if (Math.abs(den) < EPS)
        return null; const u = V.sub(c, a), t = (u[0] * s[1] - u[1] * s[0]) / den, v = (u[0] * r[1] - u[1] * r[0]) / den; return { point: V.lerp(a, b, t), t, u: v }; }
    function segmentDistance(p, a, b) { const dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy; const t = l ? Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l)) : 0; return { d: Math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy), t, point: [a[0] + t * dx, a[1] + t * dy] }; }
    function polygonArea(p) { let a = 0; for (let i = 0, j = p.length - 1; i < p.length; j = i++)
        a += p[j][0] * p[i][1] - p[i][0] * p[j][1]; return a / 2; }
    function inside(p, poly) { let yes = false; for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
        const a = poly[i], b = poly[j];
        if ((a[1] > p[1]) !== (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0])
            yes = !yes;
    } return yes; }
    function faceNormal(points) { let n = [0, 0, 0]; for (let i = 0; i < points.length; i++) {
        const a = points[i], b = points[(i + 1) % points.length];
        n[0] += (a[1] - b[1]) * (a[2] + b[2]);
        n[1] += (a[2] - b[2]) * (a[0] + b[0]);
        n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    } return V.norm(n); }
    function triangulate(points) {
        if (points.length < 3)
            return [];
        if (points.length === 3)
            return [[0, 1, 2]];
        const n = faceNormal(points), drop = Math.abs(n[0]) > Math.abs(n[1]) ? (Math.abs(n[0]) > Math.abs(n[2]) ? 0 : 2) : (Math.abs(n[1]) > Math.abs(n[2]) ? 1 : 2);
        const p = points.map(v => v.filter((_, i) => i !== drop)), sign = polygonArea(p) >= 0 ? 1 : -1, idx = p.map((_, i) => i), out = [];
        const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        let guard = points.length * points.length;
        while (idx.length > 3 && guard-- > 0) {
            let found = false;
            for (let k = 0; k < idx.length; k++) {
                const a = idx[(k + idx.length - 1) % idx.length], b = idx[k], c = idx[(k + 1) % idx.length];
                if (cross(p[a], p[b], p[c]) * sign <= EPS)
                    continue;
                let blocked = false;
                for (const j of idx) {
                    if (j === a || j === b || j === c)
                        continue;
                    const c1 = cross(p[a], p[b], p[j]) * sign, c2 = cross(p[b], p[c], p[j]) * sign, c3 = cross(p[c], p[a], p[j]) * sign;
                    if (c1 >= -EPS && c2 >= -EPS && c3 >= -EPS) {
                        blocked = true;
                        break;
                    }
                }
                if (!blocked) {
                    out.push([a, b, c]);
                    idx.splice(k, 1);
                    found = true;
                    break;
                }
            }
            if (!found) { /* Remove collinear vertices, never fabricate a fan for a bad polygon. */
                let k = idx.findIndex((b, i) => Math.abs(cross(p[idx[(i + idx.length - 1) % idx.length]], p[b], p[idx[(i + 1) % idx.length]])) < EPS);
                if (k >= 0)
                    idx.splice(k, 1);
                else
                    break;
            }
        }
        if (idx.length === 3)
            out.push(idx.slice());
        return out;
    }
    class Camera {
        constructor() { this.target = [0, 0, 0]; this.zoom = 1; this.yaw = -Math.PI / 2; this.pitch = Math.PI / 2; this.perspective = false; this.width = 1000; this.height = 700; this.revision = 0; this.update(); }
        update() { const c = Math.cos(this.pitch), n = [Math.cos(this.yaw) * c, Math.sin(this.yaw) * c, Math.sin(this.pitch)]; this.right = [-Math.sin(this.yaw), Math.cos(this.yaw), 0]; this.up = V.cross(n, this.right); this.direction = n; const worldH = this.height / this.zoom; this.distance = worldH * 1.35; this.eye = V.add(this.target, V.mul(n, this.distance)); const r = this.right, u = this.up, e = this.eye; this.view = new Float64Array([r[0], u[0], n[0], 0, r[1], u[1], n[1], 0, r[2], u[2], n[2], 0, -V.dot(r, e), -V.dot(u, e), -V.dot(n, e), 1]); const w = this.width / this.zoom, h = worldH, near = Math.max(.00001, this.distance / 100000), far = this.distance * 100; this.projection = this.perspective ? M.perspective(40 * Math.PI / 180, this.width / this.height, near, far) : M.ortho(-w / 2, w / 2, -h / 2, h / 2, near, far); this.matrix = M.multiply(this.projection, this.view); this.inverse = M.inverse(this.matrix); this.revision++; }
        resize(w, h) { if (w === this.width && h === this.height)
            return; this.width = w; this.height = h; this.update(); }
        project(p) { const m = this.matrix, x = p[0], y = p[1], z = p[2] || 0, w = m[3] * x + m[7] * y + m[11] * z + m[15]; if (w <= 0)
            return [-1e9, -1e9, 2]; const v = M.point(m, p); return [(v[0] + 1) * this.width / 2, (1 - v[1]) * this.height / 2, v[2]]; }
        ray(x, y) { const q = [x / this.width * 2 - 1, 1 - y / this.height * 2]; const a = M.point(this.inverse, [...q, 0]), b = M.point(this.inverse, [...q, 1]); return { origin: a, direction: V.norm(V.sub(b, a)) }; }
        unproject(x, y, z = 0) { const ray = this.ray(x, y), d = ray.direction[2]; if (Math.abs(d) < 1e-7)
            return null; return V.add(ray.origin, V.mul(ray.direction, (z - ray.origin[2]) / d)); }
        pointOnView(x, y) { const ray = this.ray(x, y), den = V.dot(ray.direction, this.direction); return V.add(ray.origin, V.mul(ray.direction, V.dot(V.sub(this.target, ray.origin), this.direction) / den)); }
        pan(dx, dy) { const a = this.pointOnView(this.width / 2, this.height / 2), b = this.pointOnView(this.width / 2 + dx, this.height / 2 + dy); this.target = V.sub(this.target, V.sub(b, a)); this.update(); }
        zoomAt(factor, x = this.width / 2, y = this.height / 2) { const before = this.unproject(x, y, this.target[2]) || this.pointOnView(x, y); this.zoom = Math.max(1e-7, Math.min(1e7, this.zoom * factor)); this.update(); const after = this.unproject(x, y, this.target[2]) || this.pointOnView(x, y); this.target = V.add(this.target, V.sub(before, after)); this.update(); }
        setView(name) { const views = { top: [-90, 90], bottom: [-90, -90], front: [-90, 0], back: [90, 0], right: [0, 0], left: [180, 0], iso: [-48, 32] }; const p = views[name] || views.iso; this.yaw = p[0] * Math.PI / 180; this.pitch = p[1] * Math.PI / 180; this.update(); }
        fit(points, padding = 1.22) { if (!points.length) {
            this.target = [0, 0, 0];
            this.zoom = Math.min(this.width / 2000, this.height / 1400);
            this.update();
            return;
        } let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity]; for (const p of points)
            for (let i = 0; i < 3; i++) {
                min[i] = Math.min(min[i], p[i] || 0);
                max[i] = Math.max(max[i], p[i] || 0);
            } this.target = min.map((v, i) => (v + max[i]) / 2); let x = 0, y = 0; for (const p of points) {
            const d = V.sub(p, this.target);
            x = Math.max(x, Math.abs(V.dot(d, this.right)));
            y = Math.max(y, Math.abs(V.dot(d, this.up)));
        } this.zoom = Math.min(this.width / Math.max(x * 2 * padding, 1), this.height / Math.max(y * 2 * padding, 1)); this.update(); }
        serialize() { return { target: this.target, zoom: this.zoom, yaw: this.yaw, pitch: this.pitch, perspective: this.perspective }; }
        restore(s) { if (s) {
            this.target = (s.target || [0, 0, 0]).slice();
            for (const k of ['zoom', 'yaw', 'pitch'])
                if (Number.isFinite(s[k]))
                    this[k] = s[k];
            this.perspective = !!s.perspective;
        } this.update(); }
    }
    K.Math = { EPS, TAU, V, M, basis, angle, sweep, lineIntersection, segmentDistance, polygonArea, inside, faceNormal, triangulate };
    K.Camera = Camera;
})(typeof window !== 'undefined' ? window : globalThis);
