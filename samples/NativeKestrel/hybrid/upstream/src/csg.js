/* Kestrel CAD — BSP-based polygonal constructive solid geometry.
 * Operations are on tessellated, closed meshes; this is not a B-rep kernel. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, triangulate } = K.Math;
    class Polygon {
        constructor(vertices, eps) { this.vertices = vertices; this.eps = eps; let normal = [0, 0, 0]; for (let i = 2; i < vertices.length; i++) {
            normal = V.cross(V.sub(vertices[i - 1], vertices[0]), V.sub(vertices[i], vertices[0]));
            if (V.len(normal) > eps * eps)
                break;
        } this.normal = V.norm(normal); this.distance = V.dot(this.normal, vertices[0]); }
        flip() { this.vertices.reverse(); this.normal = V.mul(this.normal, -1); this.distance = -this.distance; return this; }
        copy() { return new Polygon(this.vertices.map(p => p.slice()), this.eps); }
    }
    class Node {
        constructor(polygons = [], depth = 0) { this.plane = null; this.front = null; this.back = null; this.polygons = []; this.depth = depth; if (polygons.length)
            this.insert(polygons); }
        split(poly, coplanarFront, coplanarBack, front, back) { const { normal: n, distance: w, eps } = this.plane, dist = poly.vertices.map(p => V.dot(n, p) - w), types = dist.map(d => d > eps ? 1 : d < -eps ? 2 : 0), kind = types.reduce((a, b) => a | b, 0); if (kind === 0) {
            (V.dot(n, poly.normal) >= 0 ? coplanarFront : coplanarBack).push(poly);
            return;
        } if (kind === 1) {
            front.push(poly);
            return;
        } if (kind === 2) {
            back.push(poly);
            return;
        } const f = [], b = []; for (let i = 0; i < poly.vertices.length; i++) {
            const j = (i + 1) % poly.vertices.length, vi = poly.vertices[i], vj = poly.vertices[j], ti = types[i], tj = types[j];
            if (ti !== 2)
                f.push(vi);
            if (ti !== 1)
                b.push(vi);
            if ((ti | tj) === 3) {
                const t = dist[i] / (dist[i] - dist[j]), p = V.lerp(vi, vj, t);
                f.push(p);
                b.push(p);
            }
        } const clean = p => p.filter((v, i) => !V.same(v, p[(i + 1) % p.length], eps * .1)); const ff = clean(f), bb = clean(b); if (ff.length >= 3)
            front.push(new Polygon(ff, eps)); if (bb.length >= 3)
            back.push(new Polygon(bb, eps)); }
        insert(polygons) { if (!polygons.length)
            return; if (this.depth > 800)
            throw Error('Boolean complexity limit reached. Simplify the input meshes.'); if (!this.plane) { /* A middle polygon avoids the worst first-face bias. */
            this.plane = polygons[Math.floor(polygons.length / 2)].copy();
        } const f = [], b = []; for (const p of polygons)
            this.split(p, this.polygons, this.polygons, f, b); if (f.length) {
            if (!this.front)
                this.front = new Node([], this.depth + 1);
            this.front.insert(f);
        } if (b.length) {
            if (!this.back)
                this.back = new Node([], this.depth + 1);
            this.back.insert(b);
        } }
        all() { return [...this.polygons, ...(this.front ? this.front.all() : []), ...(this.back ? this.back.all() : [])]; }
        invert() { for (const p of this.polygons)
            p.flip(); if (this.plane)
            this.plane.flip(); if (this.front)
            this.front.invert(); if (this.back)
            this.back.invert(); [this.front, this.back] = [this.back, this.front]; }
        clip(polygons) { if (!this.plane)
            return polygons.slice(); let f = [], b = []; for (const p of polygons)
            this.split(p, f, b, f, b); if (this.front)
            f = this.front.clip(f); b = this.back ? this.back.clip(b) : []; return f.concat(b); }
        clipTo(tree) { this.polygons = tree.clip(this.polygons); if (this.front)
            this.front.clipTo(tree); if (this.back)
            this.back.clipTo(tree); }
    }
    function polygons(mesh, eps) { const out = []; for (const f of mesh.faces) {
        const p = f.map(i => mesh.vertices[i]);
        for (const t of triangulate(p)) {
            const poly = new Polygon(t.map(i => p[i].slice()), eps);
            if (V.len(poly.normal) > .5)
                out.push(poly);
        }
    } return out; }
    function boolean(a, b, operation = 'union') {
        if (a.type !== 'MESH' || b.type !== 'MESH')
            throw Error('Select two mesh solids for a Boolean operation.');
        if (a.faces.length + b.faces.length > 20000)
            throw Error('Interactive Booleans are limited to 20,000 input faces.');
        let extent = 1;
        for (const mesh of [a, b])
            for (const p of mesh.vertices)
                extent = Math.max(extent, ...p.map(Math.abs));
        const eps = Math.max(1e-7, extent * 1e-8);
        const A = new Node(polygons(a, eps)), B = new Node(polygons(b, eps));
        if (operation === 'union') {
            A.clipTo(B);
            B.clipTo(A);
            B.invert();
            B.clipTo(A);
            B.invert();
            A.insert(B.all());
        }
        else if (operation === 'subtract') {
            A.invert();
            A.clipTo(B);
            B.clipTo(A);
            B.invert();
            B.clipTo(A);
            B.invert();
            A.insert(B.all());
            A.invert();
        }
        else if (operation === 'intersect') {
            A.invert();
            B.clipTo(A);
            B.invert();
            A.clipTo(B);
            B.clipTo(A);
            A.insert(B.all());
            A.invert();
        }
        else
            throw Error('Unknown Boolean operation.');
        const vertices = [], faces = [], map = new Map();
        for (const poly of A.all()) {
            const f = [];
            for (const p of poly.vertices) {
                const key = p.map(x => Math.round(x / (eps * .25))).join(',');
                let i = map.get(key);
                if (i === undefined) {
                    i = vertices.length;
                    map.set(key, i);
                    vertices.push(p);
                }
                if (!f.length || f.at(-1) !== i)
                    f.push(i);
            }
            if (f.length > 2 && f[0] === f.at(-1))
                f.pop();
            if (new Set(f).size >= 3)
                faces.push(f);
        }
        return { type: 'MESH', primitive: operation[0].toUpperCase() + operation.slice(1), layer: a.layer, color: a.color, vertices, faces };
    }
    K.CSG = { boolean };
})(typeof window !== 'undefined' ? window : globalThis);
