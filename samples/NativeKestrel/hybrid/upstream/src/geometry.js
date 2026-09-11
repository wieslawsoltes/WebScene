/* Kestrel CAD — analytic curves, tessellation, extrusion and mesh primitives. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, EPS, TAU, basis, sweep, angle, triangulate, polygonArea, lineIntersection, faceNormal } = K.Math;
    function conicAxes(e) { if (e.axisX && e.axisY)
        return { x: e.axisX, y: e.axisY }; const b = basis(e.normal), r = e.rotation || 0; return { x: V.mul(V.add(V.mul(b.x, Math.cos(r)), V.mul(b.y, Math.sin(r))), e.rx || e.radius || 1), y: V.mul(V.add(V.mul(b.x, -Math.sin(r)), V.mul(b.y, Math.cos(r))), e.ry || e.radius || 1) }; }
    function conicPoint(e, t) { const { x, y } = conicAxes(e); return V.add(e.center, V.add(V.mul(x, Math.cos(t)), V.mul(y, Math.sin(t)))); }
    function uniformKnots(count, degree) { return Array.from({ length: count + degree + 1 }, (_, i) => i <= degree ? 0 : i >= count ? 1 : (i - degree) / (count - degree)); }
    function nurbs(e, t) { const p = e.controlPoints || e.points, degree = Math.min(e.degree || 3, p.length - 1), knots = e.knots?.length === p.length + degree + 1 ? e.knots : uniformKnots(p.length, degree), weights = e.weights || []; let k = degree; const lo = knots[degree], hi = knots[p.length], u = lo + (hi - lo) * Math.min(1, Math.max(0, t)); while (k < p.length - 1 && knots[k + 1] <= u)
        k++; const d = []; for (let j = 0; j <= degree; j++) {
        const i = k - degree + j, w = weights[i] ?? 1;
        d[j] = [p[i][0] * w, p[i][1] * w, (p[i][2] || 0) * w, w];
    } for (let r = 1; r <= degree; r++)
        for (let j = degree; j >= r; j--) {
            const i = k - degree + j, den = knots[i + degree - r + 1] - knots[i], a = den > 0 ? (u - knots[i]) / den : 0;
            d[j] = d[j].map((v, c) => (1 - a) * d[j - 1][c] + a * v);
        } return d[degree].slice(0, 3).map(v => v / (d[degree][3] || 1)); }
    function polylinePoints(e, tolerance = 1) { const p = e.points || [], out = []; for (let i = 0; i < p.length; i++) {
        out.push(p[i]);
        if (i === p.length - 1 && !e.closed)
            break;
        const b = e.bulges?.[i] || 0;
        if (Math.abs(b) < EPS)
            continue;
        const a = p[i], z = p[(i + 1) % p.length], chord = V.dist(a, z);
        if (chord < EPS)
            continue;
        const normal = e.normal || [0, 0, 1], left = V.norm(V.cross(normal, V.sub(z, a))), center = V.add(V.lerp(a, z, .5), V.mul(left, chord * (1 - b * b) / (4 * b))), radius = chord * (1 + b * b) / (4 * Math.abs(b)), s = Math.atan2(a[1] - center[1], a[0] - center[0]), delta = 4 * Math.atan(b), steps = Math.max(4, Math.min(256, Math.ceil(Math.abs(delta) * Math.sqrt(radius / Math.max(.01, tolerance)))));
        for (let k = 1; k < steps; k++) {
            const r = M.rotation(delta * k / steps, normal);
            out.push(V.add(center, M.point(r, V.sub(a, center), 0)));
        }
    } return out; }
    function path(e, tolerance = .5) { switch (e.type) {
        case 'LINE': return e.points;
        case 'POLYLINE': return polylinePoints(e, tolerance);
        case 'CIRCLE':
        case 'ARC':
        case 'ELLIPSE': {
            const a = e.startAngle || 0, d = e.type === 'ARC' || e.type === 'ELLIPSE' && e.endAngle != null ? sweep(a, e.endAngle) : TAU, axes = conicAxes(e), r = Math.max(V.len(axes.x), V.len(axes.y)), n = Math.max(24, Math.min(512, Math.ceil(d * Math.sqrt(r / Math.max(.001, tolerance)))));
            return Array.from({ length: n + (d < TAU - EPS ? 1 : 0) }, (_, i) => conicPoint(e, a + d * i / n));
        }
        case 'SPLINE': {
            if ((e.controlPoints || e.points || []).length < 2)
                return [];
            const n = Math.min(512, Math.max(32, (e.controlPoints || e.points).length * 20));
            return Array.from({ length: n + 1 }, (_, i) => nurbs(e, i / n));
        }
        case 'HATCH': return e.points;
        case 'POINT': return [e.position];
        case 'TEXT': return [e.position];
        default: return [];
    } }
    function closed(e) { return e.type === 'CIRCLE' || e.type === 'HATCH' || (e.type === 'POLYLINE' && e.closed) || (e.type === 'ELLIPSE' && (e.endAngle == null || sweep(e.startAngle || 0, e.endAngle) > TAU - EPS)); }
    function hatchSegments(e) { const p = e.points || []; if (p.length < 3)
        return []; const n = e.normal || faceNormal(p), b = basis(n), origin = p[0], flat = p.map(v => { const d = V.sub(v, origin); return [V.dot(d, b.x), V.dot(d, b.y)]; }); const result = [], spacing = Math.max(e.spacing || 10, .001), angles = e.pattern === 'cross' ? [e.angle ?? Math.PI / 4, (e.angle ?? Math.PI / 4) + Math.PI / 2] : [e.angle ?? Math.PI / 4]; for (const a of angles) {
        const c = Math.cos(a), s = Math.sin(a), q = flat.map(v => [v[0] * c + v[1] * s, -v[0] * s + v[1] * c]);
        const min = Math.min(...q.map(v => v[1])), max = Math.max(...q.map(v => v[1]));
        let step = spacing;
        if ((max - min) / step > 3000)
            step = (max - min) / 3000;
        for (let y = Math.ceil(min / step) * step; y < max; y += step) {
            const xs = [];
            for (let i = 0, j = q.length - 1; i < q.length; j = i++) {
                const v = q[i], w = q[j];
                if ((v[1] <= y && w[1] > y) || (w[1] <= y && v[1] > y))
                    xs.push(v[0] + (y - v[1]) * (w[0] - v[0]) / (w[1] - v[1]));
            }
            xs.sort((a, b) => a - b);
            const toWorld = x => V.add(origin, V.add(V.mul(b.x, x * c - y * s), V.mul(b.y, x * s + y * c)));
            for (let k = 0; k + 1 < xs.length; k += 2)
                result.push([toWorld(xs[k]), toWorld(xs[k + 1])]);
        }
    } return result; }
    function textAxes(e) { const b = basis(e.normal || [0, 0, 1]), r = e.rotation || 0, x = e.direction ? V.norm(e.direction) : V.add(V.mul(b.x, Math.cos(r)), V.mul(b.y, Math.sin(r))); return { x, y: V.norm(V.cross(b.n, x)), n: b.n }; }
    function dimension(e) { const a = e.points[0], b = e.points[1], d = V.sub(b, a), len = V.len(d), u = V.norm(d), n = V.norm(V.cross(e.normal || [0, 0, 1], u)), off = e.offset ?? len * .12, s = e.textHeight || Math.max(1, len * .025), aa = V.add(a, V.mul(n, off)), bb = V.add(b, V.mul(n, off)), mid = V.lerp(aa, bb, .5); const segments = [[V.add(a, V.mul(n, Math.sign(off) * s * .3)), V.add(aa, V.mul(n, Math.sign(off) * s * .8))], [V.add(b, V.mul(n, Math.sign(off) * s * .3)), V.add(bb, V.mul(n, Math.sign(off) * s * .8))], [aa, bb]]; for (const [p, dir] of [[aa, 1], [bb, -1]]) {
        segments.push([p, V.add(p, V.add(V.mul(u, dir * s), V.mul(n, s * .28)))]);
        segments.push([p, V.add(p, V.add(V.mul(u, dir * s), V.mul(n, -s * .28)))]);
    } return { segments, text: { position: V.add(mid, V.mul(n, s * .5)), text: e.text || len.toFixed(e.precision ?? 0), height: s, rotation: Math.atan2(u[1], u[0]), direction: u, normal: e.normal || [0, 0, 1], align: 'center' } }; }
    function featureEdges(vertices, used, all = false) {
        if (all)
            return [...used.values()].map(e => [vertices[e.a], vertices[e.b]]);
        const result = [], groups = new Map(), base = vertices[0] || [0, 0, 0];
        let scale = 1;
        for (const p of vertices)
            scale = Math.max(scale, V.dist(p, base));
        const eps = scale * 1e-7;
        for (const edge of used.values()) {
            if (edge.count > 1) {
                if (edge.crease)
                    result.push([vertices[edge.a], vertices[edge.b]]);
                continue;
            }
            const a = vertices[edge.a], b = vertices[edge.b];
            let dir = V.norm(V.sub(b, a));
            if (V.len(dir) < .5)
                continue;
            const axis = dir.findIndex(v => Math.abs(v) > 1e-5);
            if (dir[axis] < 0)
                dir = V.mul(dir, -1);
            const moment = V.cross(V.sub(a, base), dir), key = dir.map(v => Math.round(v * 1e6)).join(',') + ':' + moment.map(v => Math.round(v / eps)).join(',');
            let group = groups.get(key);
            if (!group) {
                group = { dir, origin: a, edges: [] };
                groups.set(key, group);
            }
            const ta = V.dot(V.sub(a, group.origin), group.dir), tb = V.dot(V.sub(b, group.origin), group.dir);
            group.edges.push({ lo: Math.min(ta, tb), hi: Math.max(ta, tb), n: edge.n });
        }
        for (const group of groups.values()) {
            const values = group.edges.flatMap(e => [e.lo, e.hi]).sort((a, b) => a - b), ts = values.filter((t, i) => !i || Math.abs(t - values[i - 1]) > eps);
            for (let i = 0; i < ts.length - 1; i++) {
                const lo = ts[i], hi = ts[i + 1], mid = (lo + hi) / 2, cover = group.edges.filter(e => mid > e.lo - eps && mid < e.hi + eps);
                if (!cover.length)
                    continue;
                if (cover.length === 1 || cover.some(e => Math.abs(V.dot(e.n, cover[0].n)) < .98))
                    result.push([V.add(group.origin, V.mul(group.dir, lo)), V.add(group.origin, V.mul(group.dir, hi))]);
            }
        }
        return result;
    }
    function geometry(e, tolerance = .5) {
        const segments = [], triangles = [], texts = [], snaps = [], wireSegments = [];
        if (e.type === 'MESH') {
            const used = new Map();
            for (const face of e.faces) {
                if (face.length < 3)
                    continue;
                const p = face.map(i => e.vertices[i]).filter(Boolean);
                if (p.length !== face.length)
                    continue;
                const n = faceNormal(p);
                for (const t of triangulate(p))
                    triangles.push({ points: t.map(i => p[i]), normal: n });
                for (let i = 0; i < face.length; i++) {
                    const a = face[i], b = face[(i + 1) % face.length], key = a < b ? a + ':' + b : b + ':' + a;
                    let v = used.get(key);
                    if (!v) {
                        used.set(key, { a, b, n, count: 1 });
                    }
                    else {
                        v.count++;
                        if (Math.abs(V.dot(v.n, n)) < .98)
                            v.crease = true;
                    }
                }
            }
            for (const edge of used.values())
                wireSegments.push([e.vertices[edge.a], e.vertices[edge.b]]);
            for (const s of featureEdges(e.vertices, used, e.showAllEdges))
                segments.push(s);
            for (const p of e.vertices)
                snaps.push({ point: p, type: 'endpoint' });
        }
        else if (e.type === 'DIMENSION') {
            const d = dimension(e);
            segments.push(...d.segments);
            texts.push(d.text);
            for (const p of e.points)
                snaps.push({ point: p, type: 'endpoint' });
        }
        else if (e.type === 'TEXT') {
            texts.push(e);
            snaps.push({ point: e.position, type: 'insertion' });
        }
        else if (e.type === 'POINT') {
            snaps.push({ point: e.position, type: 'node' });
            const s = e.size || 2;
            segments.push([V.add(e.position, [-s, 0, 0]), V.add(e.position, [s, 0, 0])], [V.add(e.position, [0, -s, 0]), V.add(e.position, [0, s, 0])]);
        }
        else {
            const p = path(e, tolerance), isClosed = closed(e);
            for (let i = 1; i < p.length; i++)
                segments.push([p[i - 1], p[i]]);
            if (isClosed && p.length > 2)
                segments.push([p[p.length - 1], p[0]]);
            if (e.type === 'HATCH') {
                if (e.pattern === 'solid') {
                    const n = faceNormal(p);
                    for (const t of triangulate(p))
                        triangles.push({ points: t.map(i => p[i]), normal: n });
                }
                else
                    segments.push(...hatchSegments(e));
            }
            if (e.center) {
                snaps.push({ point: e.center, type: 'center' });
                for (let i = 0; i < 4; i++) {
                    const t = i * Math.PI / 2;
                    if (e.type !== 'ARC' || angle(t - (e.startAngle || 0)) <= sweep(e.startAngle || 0, e.endAngle))
                        snaps.push({ point: conicPoint(e, t), type: 'quadrant' });
                }
                if (e.type === 'ARC') {
                    snaps.push({ point: p[0], type: 'endpoint' }, { point: p[p.length - 1], type: 'endpoint' });
                }
            }
            else if (e.type === 'SPLINE') {
                if (p.length)
                    snaps.push({ point: p[0], type: 'endpoint' }, { point: p[p.length - 1], type: 'endpoint' });
            }
            else {
                const control = e.points || p;
                for (let i = 0; i < control.length; i++) {
                    snaps.push({ point: control[i], type: 'endpoint' });
                    if (i < control.length - 1 || isClosed)
                        snaps.push({ point: V.lerp(control[i], control[(i + 1) % control.length], .5), type: 'midpoint' });
                }
            }
        }
        const points = [];
        for (const s of segments)
            points.push(...s);
        for (const t of triangles)
            points.push(...t.points);
        for (const t of texts) {
            const axes = textAxes(t), h = t.height || 10, lines = (t.text || '').split('\n'), w = Math.max(...lines.map(l => l.length)) * h * .66, left = t.align === 'center' ? -w / 2 : t.align === 'right' ? -w : 0;
            for (const x of [left, left + w])
                for (const y of [-h * .25 - (lines.length - 1) * h * 1.35, h * .85])
                    points.push(V.add(t.position, V.add(V.mul(axes.x, x), V.mul(axes.y, y))));
        }
        return { segments, wireSegments, triangles, texts, snaps, points };
    }
    function transform(e, m) {
        const out = JSON.parse(JSON.stringify(e)), inverse = M.inverse(m);
        if (!inverse)
            throw Error('A transform cannot collapse an axis to zero.');
        for (const key of ['points', 'controlPoints', 'vertices'])
            if (out[key])
                out[key] = out[key].map(p => M.point(m, p));
        if (out.position)
            out.position = M.point(m, out.position);
        const sx = V.len(M.point(m, [1, 0, 0], 0)), sy = V.len(M.point(m, [0, 1, 0], 0));
        const det = V.dot(M.point(m, [1, 0, 0], 0), V.cross(M.point(m, [0, 1, 0], 0), M.point(m, [0, 0, 1], 0)));
        if (out.center) {
            const a = conicAxes(e);
            out.center = M.point(m, out.center);
            out.axisX = M.point(m, a.x, 0);
            out.axisY = M.point(m, a.y, 0);
            out.radius = out.rx = V.len(out.axisX);
            out.ry = V.len(out.axisY);
            out.normal = V.norm(V.cross(out.axisX, out.axisY));
            if ((Math.abs(out.rx - out.ry) > EPS * Math.max(out.rx, 1) || Math.abs(V.dot(out.axisX, out.axisY)) > EPS * out.rx * out.ry) && ['CIRCLE', 'ARC'].includes(out.type)) {
                out.type = 'ELLIPSE';
                if (e.type === 'ARC') {
                    out.startAngle = e.startAngle;
                    out.endAngle = e.endAngle;
                }
            }
        }
        else if (['POLYLINE', 'HATCH', 'DIMENSION', 'TEXT'].includes(e.type)) {
            const normal = e.normal || (e.type === 'HATCH' ? faceNormal(e.points) : [0, 0, 1]);
            out.normal = V.norm([inverse[0] * normal[0] + inverse[1] * normal[1] + inverse[2] * normal[2], inverse[4] * normal[0] + inverse[5] * normal[1] + inverse[6] * normal[2], inverse[8] * normal[0] + inverse[9] * normal[1] + inverse[10] * normal[2]]);
            if (e.type === 'POLYLINE' && e.bulges?.some(Boolean)) {
                const b = basis(normal), x = M.point(m, b.x, 0), y = M.point(m, b.y, 0);
                if (Math.abs(V.len(x) - V.len(y)) > EPS * Math.max(V.len(x), 1) || Math.abs(V.dot(x, y)) > EPS * V.len(x) * V.len(y)) {
                    out.points = path(e, .1).map(p => M.point(m, p));
                    delete out.bulges;
                }
            }
            if (e.type === 'HATCH') {
                const b = basis(normal), bb = basis(out.normal), angle = e.angle ?? Math.PI / 4, dir = M.point(m, V.add(V.mul(b.x, Math.cos(angle)), V.mul(b.y, Math.sin(angle))), 0), x = M.point(m, b.x, 0), y = M.point(m, b.y, 0);
                out.angle = Math.atan2(V.dot(dir, bb.y), V.dot(dir, bb.x));
                out.spacing = (e.spacing || 10) * V.len(V.cross(x, y)) / Math.max(V.len(dir), EPS);
            }
            if (e.type === 'DIMENSION') {
                const oldSide = V.norm(V.cross(normal, V.sub(e.points[1], e.points[0]))), side = V.norm(V.cross(out.normal, V.sub(out.points[1], out.points[0])));
                out.offset = (e.offset ?? V.dist(...e.points) * .12) * V.dot(M.point(m, oldSide, 0), side);
            }
        }
        if (out.height)
            out.height *= sy;
        if (out.textHeight)
            out.textHeight *= sy;
        if (out.type === 'TEXT') {
            const axes = textAxes(e), x = M.point(m, axes.x, 0), y = M.point(m, axes.y, 0), b = basis(out.normal);
            out.direction = V.norm(x);
            out.rotation = Math.atan2(V.dot(x, b.y), V.dot(x, b.x));
            out.height = (e.height || 10) * V.len(y);
        }
        if (det < 0) {
            if (out.faces)
                out.faces = out.faces.map(f => f.slice().reverse());
            if (out.bulges)
                out.bulges = out.bulges.map(x => -x);
        }
        return out;
    }
    function bounds(entities) { const points = []; for (const e of entities)
        for (const p of geometry(e, 1).points)
            points.push(p); return points; }
    function box(p, w, d, h) { const [x, y, z] = p, vertices = [[x, y, z], [x + w, y, z], [x + w, y + d, z], [x, y + d, z], [x, y, z + h], [x + w, y, z + h], [x + w, y + d, z + h], [x, y + d, z + h]]; return { type: 'MESH', primitive: 'Box', vertices, faces: [[3, 2, 1, 0], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]] }; }
    function cylinder(p, r, h, n = 64, topRadius = r) { const vertices = [], faces = []; for (let j = 0; j < 2; j++)
        for (let i = 0; i < n; i++) {
            const a = i / n * TAU, rr = j ? topRadius : r;
            vertices.push(V.add(p, [Math.cos(a) * rr, Math.sin(a) * rr, j * h]));
        } faces.push(Array.from({ length: n }, (_, i) => n - i - 1)); if (topRadius > EPS)
        faces.push(Array.from({ length: n }, (_, i) => n + i)); for (let i = 0; i < n; i++)
        faces.push([i, (i + 1) % n, (i + 1) % n + n, i + n]); return { type: 'MESH', primitive: topRadius === r ? 'Cylinder' : 'Cone', vertices, faces }; }
    function sphere(p, r, n = 40, rings = 20) { const vertices = [], faces = []; vertices.push(V.add(p, [0, 0, -r])); for (let j = 1; j < rings; j++) {
        const t = -Math.PI / 2 + j / rings * Math.PI;
        for (let i = 0; i < n; i++)
            vertices.push(V.add(p, [r * Math.cos(t) * Math.cos(i / n * TAU), r * Math.cos(t) * Math.sin(i / n * TAU), r * Math.sin(t)]));
    } const top = vertices.length; vertices.push(V.add(p, [0, 0, r])); for (let i = 0; i < n; i++) {
        faces.push([0, 1 + (i + 1) % n, 1 + i]);
        for (let j = 0; j < rings - 2; j++) {
            const a = 1 + j * n + i, b = 1 + j * n + (i + 1) % n;
            faces.push([a, b, b + n, a + n]);
        }
        faces.push([1 + (rings - 2) * n + i, 1 + (rings - 2) * n + (i + 1) % n, top]);
    } return { type: 'MESH', primitive: 'Sphere', vertices, faces, smooth: true }; }
    function torus(p, major, minor, n = 64, m = 20) { const vertices = [], faces = []; for (let i = 0; i < n; i++)
        for (let j = 0; j < m; j++) {
            const a = i / n * TAU, b = j / m * TAU;
            vertices.push(V.add(p, [(major + minor * Math.cos(b)) * Math.cos(a), (major + minor * Math.cos(b)) * Math.sin(a), minor * Math.sin(b)]));
        } for (let i = 0; i < n; i++)
        for (let j = 0; j < m; j++)
            faces.push([i * m + j, ((i + 1) % n) * m + j, ((i + 1) % n) * m + (j + 1) % m, i * m + (j + 1) % m]); return { type: 'MESH', primitive: 'Torus', vertices, faces, smooth: true }; }
    function extrude(points, height, normal = null) { if (!Number.isFinite(height) || Math.abs(height) < EPS)
        throw Error('Extrusion height cannot be zero.'); let p = points.map(v => v.slice()); if (p.length > 2 && V.same(p[0], p[p.length - 1]))
        p.pop(); p = p.filter((v, i) => !i || !V.same(v, p[i - 1])); let changed = true; while (changed && p.length > 3) {
        changed = false;
        for (let i = 0; i < p.length; i++) {
            const a = V.sub(p[i], p[(i + p.length - 1) % p.length]), b = V.sub(p[(i + 1) % p.length], p[i]);
            if (V.len(V.cross(a, b)) < EPS * Math.max(1, V.len(a) * V.len(b)) && V.dot(a, b) >= 0) {
                p.splice(i, 1);
                changed = true;
                break;
            }
        }
    } if (p.length < 3)
        throw Error('Extrusion needs at least three distinct boundary points.'); let n = normal ? V.norm(normal) : faceNormal(p); if (V.len(n) < EPS)
        throw Error('The profile is degenerate.'); const extent = Math.max(1, ...p.map(q => V.dist(q, p[0]))); if (p.some(q => Math.abs(V.dot(V.sub(q, p[0]), n)) > extent * 1e-7))
        throw Error('Extrusion requires a planar profile.'); if (height < 0)
        n = V.mul(n, -1); const h = Math.abs(height), vertices = [...p, ...p.map(v => V.add(v, V.mul(n, h)))], faces = [], cap = triangulate(p); if (cap.length < p.length - 2)
        throw Error('Profile must be a simple, non-self-intersecting polygon.'); const same = V.dot(faceNormal(p), n) > 0; for (const t of cap) {
        faces.push(same ? t.slice().reverse() : t.slice());
        faces.push((same ? t : t.slice().reverse()).map(i => i + p.length));
    } for (let i = 0; i < p.length; i++) {
        const j = (i + 1) % p.length, f = [i, j, j + p.length, i + p.length];
        faces.push(same ? f : f.reverse());
    } return { type: 'MESH', primitive: 'Extrusion', vertices, faces }; }
    function revolve(points, origin, axis, degrees = 360, segments = 64) { const a = V.norm(axis); if (V.len(a) < EPS)
        throw Error('Revolution axis cannot have zero length.'); const normal = faceNormal(points), extent = Math.max(1, ...points.map(p => V.dist(p, points[0]))), tol = extent * 1e-7; if (points.length < 3 || V.len(normal) < EPS)
        throw Error('Revolution requires a nondegenerate closed profile.'); if (points.some(p => Math.abs(V.dot(V.sub(p, points[0]), normal)) > tol) || Math.abs(V.dot(a, normal)) > 1e-6 || Math.abs(V.dot(V.sub(origin, points[0]), normal)) > tol)
        throw Error('The revolution axis must lie in the planar profile.'); const radial = V.cross(normal, a), distances = points.map(p => V.dot(V.sub(p, origin), radial)); if (Math.min(...distances) < -tol && Math.max(...distances) > tol)
        throw Error('The revolution profile must not cross the axis.'); if (!Number.isFinite(degrees) || Math.abs(degrees) < 1e-7 || Math.abs(degrees) > 360 || !Number.isInteger(segments) || segments < 3 || segments > 512)
        throw Error('Invalid revolution sweep or segment count.'); const full = Math.abs(degrees) >= 359.999, n = segments, count = points.length, vertices = [], faces = []; for (let j = 0; j <= (full ? n - 1 : n); j++) {
        const m = M.around(origin, M.rotation(degrees * Math.PI / 180 * j / n, a));
        for (const p of points)
            vertices.push(M.point(m, p));
    } for (let j = 0; j < n; j++)
        for (let i = 0; i < count; i++) {
            const k = (i + 1) % count, next = (j + 1) % (full ? n : n + 1);
            faces.push([j * count + i, j * count + k, next * count + k, next * count + i]);
        } if (!full) {
        faces.push(Array.from({ length: count }, (_, i) => count - 1 - i));
        faces.push(Array.from({ length: count }, (_, i) => n * count + i));
    } let result = { type: 'MESH', primitive: 'Revolution', vertices, faces }; if (volume(result) < 0)
        result.faces = result.faces.map(f => f.reverse()); return result; }
    function volume(e) { if (e.type !== 'MESH')
        return 0; let v = 0; for (const face of e.faces) {
        const p = face.map(i => e.vertices[i]);
        for (const t of triangulate(p))
            v += V.dot(p[t[0]], V.cross(p[t[1]], p[t[2]])) / 6;
    } return v; }
    function offset(e, d) { if (['CIRCLE', 'ARC'].includes(e.type)) {
        const r = (e.radius || V.len(conicAxes(e).x)) + d;
        if (r <= EPS)
            throw Error('Offset would create a non-positive radius.');
        const out = JSON.parse(JSON.stringify(e)), f = r / (e.radius || V.len(conicAxes(e).x));
        out.radius = r;
        if (out.axisX)
            out.axisX = V.mul(out.axisX, f);
        if (out.axisY)
            out.axisY = V.mul(out.axisY, f);
        return out;
    } if (!['LINE', 'POLYLINE'].includes(e.type))
        throw Error('Offset supports lines, polylines, circles and arcs.'); const p = e.type === 'POLYLINE' && e.bulges?.some(Boolean) ? path(e) : e.points, n = p.length, close = e.closed; const side = close ? (polygonArea(p) > 0 ? -d : d) : d, segments = []; for (let i = 0; i < n - (close ? 0 : 1); i++) {
        const a = p[i], b = p[(i + 1) % n], v = V.sub(b, a), l = Math.hypot(v[0], v[1]);
        if (l < EPS)
            throw Error('The profile contains a zero-length segment.');
        const off = [-v[1] / l * side, v[0] / l * side, 0];
        segments.push([V.add(a, off), V.add(b, off)]);
    } const out = []; for (let i = 0; i < n; i++) {
        if (!close && i === 0) {
            out.push(segments[0][0]);
            continue;
        }
        if (!close && i === n - 1) {
            out.push(segments.at(-1)[1]);
            continue;
        }
        const prev = segments[(i - 1 + segments.length) % segments.length], cur = segments[i % segments.length], hit = lineIntersection(...prev, ...cur);
        out.push(hit && V.dist(hit.point, p[i]) < Math.abs(d) * 20 ? hit.point : V.lerp(prev[1], cur[0], .5));
    } return { ...JSON.parse(JSON.stringify(e)), points: out, bulges: undefined }; }
    function arcThrough(a, b, c) { const ab = V.lerp(a, b, .5), bc = V.lerp(b, c, .5), u = V.sub(b, a), v = V.sub(c, b), hit = lineIntersection(ab, V.add(ab, [-u[1], u[0], 0]), bc, V.add(bc, [-v[1], v[0], 0])); if (!hit)
        throw Error('Three arc points must not be collinear.'); const center = hit.point, aa = Math.atan2(a[1] - center[1], a[0] - center[0]), bb = Math.atan2(b[1] - center[1], b[0] - center[0]), cc = Math.atan2(c[1] - center[1], c[0] - center[0]); return { type: 'ARC', center, radius: V.dist(center, a), startAngle: angle(bb - aa) <= angle(cc - aa) ? aa : cc, endAngle: angle(bb - aa) <= angle(cc - aa) ? cc : aa }; }
    K.Geo = { textAxes, conicAxes, conicPoint, uniformKnots, nurbs, path, closed, geometry, transform, bounds, box, cylinder, sphere, torus, extrude, revolve, volume, offset, arcThrough, dimension };
})(typeof window !== 'undefined' ? window : globalThis);
