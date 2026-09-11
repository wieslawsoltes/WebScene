/* Kestrel CAD — persistent drafting objects. Original MIT-licensed implementation. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, EPS, TAU, basis, angle, sweep } = K.Math;
    const G = K.Geo, clone = K.clone, owners = new WeakMap();
    const baseGeometry = G.geometry, baseTransform = G.transform, baseDimension = G.dimension;
    const MM = { mm: 1, cm: 10, m: 1000, in: 25.4, ft: 304.8, unitless: 1 };
    const CUSTOM = new Set(['INSERT', 'TABLE', 'LEADER']);
    const num = (x, label = 'Number', min = -1e12, max = 1e12) => {
        if (typeof x !== 'number' || !Number.isFinite(x) || x < min || x > max) throw Error(label + ' is outside its supported range.');
        return x;
    };
    const point = p => {
        if (!Array.isArray(p) || p.length !== 3) throw Error('Expected an XYZ point.');
        p.forEach(v => num(v, 'Coordinate')); return p;
    };
    const name = s => {
        if (typeof s !== 'string' || !s.trim() || s.length > 255 || /[\x00-\x1f<>/\\":;?*|=]/.test(s) || ['__proto__', 'constructor', 'prototype'].includes(s)) throw Error('Invalid name.');
        return s.trim();
    };
    const empty = () => ({ segments: [], wireSegments: [], triangles: [], texts: [], snaps: [], points: [] });
    const merge = (out, g) => { for (const key of Object.keys(out)) for (const v of g[key] || []) out[key].push(v); return out; };
    function defaults() {
        return { schema: 2, blocks: [], references: [], dimstyles: [{ name: 'STANDARD', textHeight: 2.5, precision: 2, scale: 1, prefix: '', suffix: '' }],
            layouts: [], ucs: { origin: [0, 0, 0], x: [1, 0, 0], y: [0, 1, 0] } };
    }
    function ensure(doc) {
        if (!doc.production) doc.production = defaults();
        for (const e of doc.entities || []) owners.set(e, doc);
        return doc.production;
    }
    function frame(ucs) {
        point(ucs.origin); point(ucs.x); point(ucs.y);
        const x = V.norm(ucs.x), y = V.norm(ucs.y);
        if (V.len(ucs.x) < EPS || V.len(ucs.y) < EPS || Math.abs(V.dot(x, y)) > 1e-7) throw Error('UCS axes must be nonzero and perpendicular.');
        const m = M.identity(); m.set(x, 0); m.set(y, 4); m.set(V.cross(x, y), 8); m.set(ucs.origin, 12); return m;
    }
    const toWorld = (doc, p) => M.point(frame(ensure(doc).ucs), p);
    const toLocal = (doc, p) => M.point(M.inverse(frame(ensure(doc).ucs)), p);
    function validate(data) {
        const p = data.production || defaults();
        if (!p || p.schema !== 2) throw Error('Unsupported production schema.');
        for (const field of ['blocks', 'references', 'dimstyles', 'layouts']) if (!Array.isArray(p[field]) || p[field].length > 2048) throw Error('Invalid production ' + field + '.');
        frame(p.ucs);
        const ids = new Set(), labels = new Set(); let count = 0;
        for (const b of p.blocks) {
            if (typeof b.id !== 'string' || !/^[-\w.:]{1,128}$/.test(b.id) || ids.has(b.id)) throw Error('Invalid block identifier.');
            name(b.name); if (labels.has(b.name.toLowerCase())) throw Error('Duplicate block name.');
            ids.add(b.id); labels.add(b.name.toLowerCase());
            if (!Array.isArray(b.entities) || (count += b.entities.length) > 200000) throw Error('Block entity limit exceeded.');
            // Validate definition geometry with the same validator as model-space geometry.
            const nested = { ...data, production: undefined, entities: b.entities };
            K.validateProject(nested, true);
            if (b.attributes && (!Array.isArray(b.attributes) || b.attributes.length > 1024)) throw Error('Invalid attribute table.');
            const tags = new Set();
            for (const a of b.attributes || []) {
                name(a.tag); if (tags.has(a.tag)) throw Error('Duplicate attribute tag.'); tags.add(a.tag);
                point(a.position); num(a.height, 'Attribute height', 1e-7, 1e9);
                if (typeof a.value !== 'string' || a.value.length > 10000) throw Error('Invalid attribute value.');
            }
        }
        const blocks = new Map(p.blocks.map(b => [b.id, b]));
        function visit(id, stack = []) {
            if (!blocks.has(id)) throw Error('Missing block definition: ' + id);
            if (stack.includes(id) || stack.length >= 16) throw Error('Cyclic or over-deep block reference.');
            for (const e of blocks.get(id).entities) if (e.type === 'INSERT') visit(e.block, [...stack, id]);
        }
        for (const b of p.blocks) visit(b.id);
        for (const e of data.entities || []) if (e.type === 'INSERT' && !blocks.has(e.block)) throw Error('Missing block definition: ' + e.block);
        const styleNames = new Set();
        for (const s of p.dimstyles) {
            name(s.name); if (styleNames.has(s.name)) throw Error('Duplicate dimension style.'); styleNames.add(s.name);
            num(s.textHeight, 'Text height', 1e-7, 1e9); num(s.precision, 'Precision', 0, 8);
            if (!Number.isInteger(s.precision)) throw Error('Precision must be integral.');
            num(s.scale, 'Dimension scale', 1e-7, 1e9);
            for (const k of ['prefix', 'suffix']) if (typeof s[k] !== 'string' || s[k].length > 255) throw Error('Invalid dimension affix.');
        }
        for (const l of p.layouts) validateLayout(l);
        for (const r of p.references) if (!blocks.has(r.block) || typeof r.name !== 'string' || typeof r.loaded !== 'boolean') throw Error('Invalid reference.');
        data.production = p;
    }
    function validateEntity(e) {
        if (e.type === 'INSERT') {
            if (typeof e.block !== 'string' || !Array.isArray(e.matrix) || e.matrix.length !== 16) throw Error('Invalid block reference.');
            e.matrix.forEach(x => num(x, 'Matrix coefficient')); if (!M.inverse(e.matrix) || Math.abs(e.matrix[3]) + Math.abs(e.matrix[7]) + Math.abs(e.matrix[11]) > 1e-10 || Math.abs(e.matrix[15] - 1) > 1e-10) throw Error('Invalid affine block transform.');
            if (e.attributes && (typeof e.attributes !== 'object' || Array.isArray(e.attributes) || Object.keys(e.attributes).length > 1024)) throw Error('Invalid attributes.');
            for (const [k, v] of Object.entries(e.attributes || {})) { name(k); if (typeof v !== 'string' || v.length > 10000) throw Error('Invalid attribute value.'); }
        }
        if (e.type === 'TABLE') {
            point(e.position); num(e.rowHeight, 'Row height', 1e-7, 1e8); num(e.columnWidth, 'Column width', 1e-7, 1e8);
            if (!Array.isArray(e.cells) || !e.cells.length || e.cells.length > 1000 || !e.cells[0]?.length || e.cells[0].length > 100 || !e.cells.every(r => Array.isArray(r) && r.length === e.cells[0].length && r.every(v => typeof v === 'string' && v.length <= 10000))) throw Error('Invalid table cells.');
        }
        if (e.type === 'LEADER') {
            if (!Array.isArray(e.points) || e.points.length < 2 || e.points.length > 10000) throw Error('Invalid leader points.'); e.points.forEach(point);
            if (typeof e.text !== 'string' || e.text.length > 10000) throw Error('Invalid leader text.');
        }
        if (e.loops) { if (!Array.isArray(e.loops) || e.loops.length > 512) throw Error('Invalid hatch loops.'); for (const loop of e.loops) { if (!Array.isArray(loop) || loop.length < 3 || loop.length > 10000) throw Error('Invalid hatch loop.'); loop.forEach(point); } }
        if (e.anchors) {
            if (!Array.isArray(e.anchors) || e.anchors.length > 100) throw Error('Invalid anchors.');
            for (const a of e.anchors) if (!a || typeof a.entity !== 'string' || !['point', 'center'].includes(a.kind) || a.kind === 'point' && (!Number.isInteger(a.index) || a.index < 0)) throw Error('Invalid anchor.');
        }
        if (e.kind && e.type === 'DIMENSION' && !['aligned', 'linear', 'radius', 'diameter', 'angular', 'ordinate-x', 'ordinate-y'].includes(e.kind)) throw Error('Unknown dimension mode.');
        if (e.measureAxis) point(e.measureAxis);
    }
    function bind(doc) {
        ensure(doc);
        for (const b of doc.production.blocks) for (const e of b.entities) owners.set(e, doc);
    }
    function expand(doc, insert, stack = [], budget = { n: 0 }, display = false) {
        const p = ensure(doc), b = p.blocks.find(b => b.id === insert.block);
        if (!b) throw Error('Missing block definition: ' + insert.block);
        if (display && !doc.visible(insert)) return [];
        if (stack.includes(b.id) || stack.length >= 16) throw Error('Cyclic block.');
        if (p.references.some(r => r.block === b.id && !r.loaded)) return [];
        const out = [];
        const source = K.DynamicBlocks && b.dynamic ? K.DynamicBlocks.evaluate(b, insert.parameters || {}, doc, insert) : b.entities;
        for (const item of source) {
            if (++budget.n > 200000) throw Error('Expanded block entity limit exceeded.');
            const e = clone(item); owners.set(e,doc); if (e.layer === '0') e.layer = insert.layer;
            if (e.color === 'byblock') e.color = insert.color;
            const children = e.type === 'INSERT' ? expand(doc, e, [...stack, b.id], budget, display) : [e];
            for (const child of children) if (!display || doc.visible(child)){owners.set(child,doc);out.push(G.transform(child, insert.matrix));}
        }
        for (const a of b.dynamic && K.DynamicBlocks ? [] : b.attributes || []) if (!a.hidden) {const text={...a,type:'TEXT',position:a.position,text:insert.attributes?.[a.tag]??a.value,height:a.height,rotation:a.rotation||0,layer:insert.layer,color:insert.color,attributeTag:a.tag};owners.set(text,doc);out.push(G.transform(text,insert.matrix));}
        return out;
    }
    const expansionCache = new WeakMap();
    function* renderEntities(doc) {
        let state = expansionCache.get(doc);
        if (!state || state.revision !== doc.revision) {
            state = {revision:doc.revision, values:new WeakMap()}; expansionCache.set(doc,state);
        }
        for (const entity of doc.entities) {
            if (!doc.visible(entity)) continue;
            if (entity.type !== 'INSERT') { yield {e:entity, owner:entity.id}; continue; }
            let children = state.values.get(entity);
            if (!children) { children=expand(doc,entity,[],{n:0},true); state.values.set(entity,children); }
            for (const child of children) { owners.set(child,doc); yield {e:child,owner:entity.id}; }
        }
    }
    function defineBlock(doc, label, ids, base = [0, 0, 0], replace = true) {
        name(label); point(base); const p = ensure(doc);
        if (p.blocks.some(b => b.name.toLowerCase() === label.toLowerCase())) throw Error('Block already exists.');
        const selected = ids.map(id => doc.byId.get(id));
        if (!selected.length || selected.some(e => !e || !doc.editable(e))) throw Error('Select editable block geometry.');
        const inverse = M.translation(...V.mul(base, -1)), id = K.uid('block'); let instance;
        doc.transaction('Create block', () => {
            const entities = selected.map(e => G.transform(e, inverse));
            p.blocks.push({ id, name: label, entities, attributes: [] });
            if (replace) { doc.remove(ids); instance = doc.add('INSERT', { block: id, matrix: Array.from(M.translation(...base)), attributes: {} }); doc.selection = new Set([instance.id]); }
        }); return p.blocks.find(b => b.id === id);
    }
    function insertBlock(doc, id, position = [0, 0, 0], scale = 1, rotation = 0, attributes = {}) {
        const b = ensure(doc).blocks.find(b => b.id === id); if (!b) throw Error('Unknown block.');
        point(position); num(scale, 'Scale', 1e-7, 1e9); num(rotation); let e;
        doc.transaction('Insert block', () => { e = doc.add('INSERT', { block: id, matrix: Array.from(M.multiply(M.translation(...position), M.multiply(M.rotation(rotation), M.scale(scale)))), attributes: clone(attributes) }); doc.selection = new Set([e.id]); }); return e;
    }
    function explode(doc, ids) {
        const added = [];
        doc.transaction('Explode production objects', () => {
            for (const id of ids) { const e = doc.byId.get(id); if (!e || !doc.editable(e)) throw Error('Object is not editable.');
                const contents = e.type === 'INSERT' ? expand(doc, e) : flattenGeometry(e, doc);
                for (const c of contents) { const v = clone(c); delete v.id; delete v.sourceHandle; added.push(doc.add(v).id); }
                doc.remove([id]);
            } doc.selection = new Set(added);
        }); return added;
    }
    function attach(doc, data, label) {
        K.validateProject(data); name(label); const source = K.Drawing.from(data), factor = MM[source.units] / MM[doc.units], blockId = K.uid('xref'); let instance;
        const entities = source.entities.flatMap(e => e.type === 'INSERT' ? expand(source, e) : [e]).map(e => G.transform(e, M.scale(factor)));
        doc.transaction('Attach reference', () => {
            ensure(doc).blocks.push({ id: blockId, name: label, entities, attributes: [] });
            doc.production.references.push({ block: blockId, name: label, loaded: true, units: source.units });
            instance = doc.add('INSERT', { block: blockId, matrix: Array.from(M.identity()), attributes: {} });
        }); return instance;
    }
    function reference(doc, blockId, action, replacement) {
        const p = ensure(doc), r = p.references.find(x => x.block === blockId), b = p.blocks.find(x => x.id === blockId);
        if (!r || !b) throw Error('Unknown external reference.');
        doc.transaction('Reference ' + action, () => {
            if (action === 'unload' || action === 'load') r.loaded = action === 'load';
            else if (action === 'bind') p.references = p.references.filter(x => x !== r);
            else if (action === 'detach') {
                if (p.blocks.some(x => x.entities.some(e => e.block === blockId))) throw Error('Reference is used by a nested block. Bind it instead.');
                doc.remove(doc.entities.filter(e => e.block === blockId).map(e => e.id)); p.references = p.references.filter(x => x !== r); p.blocks = p.blocks.filter(x => x !== b);
            } else if (action === 'reload') { const source = K.Drawing.from(replacement), factor = MM[source.units] / MM[doc.units]; b.entities = source.entities.flatMap(e => e.type === 'INSERT' ? expand(source, e) : [e]).map(e => G.transform(e, M.scale(factor))); r.loaded = true; r.units = source.units; }
            else throw Error('Unknown reference operation.');
        });
    }
    function associations(doc) {
        for (const e of doc.entities) if (e.anchors?.length) {
            let broken = false;
            const p = e.anchors.map(a => { const source = doc.byId.get(a.entity), value = a.kind === 'center' ? source?.center : source?.points?.[a.index]; if (!value) broken = true; return value && value.slice(); });
            e.associationBroken = broken;
            if (!broken && e.type === 'DIMENSION') e.points = p;
        }
        for (const e of doc.entities) if (e.type === 'HATCH' && e.boundaryIds?.length) {
            const sources = e.boundaryIds.map(id => doc.byId.get(id)); e.associationBroken = sources.some(s => !s || !G.closed(s));
            if (!e.associationBroken) { e.loops = sources.map(s => clone(G.path(s, .1))); e.points = clone(e.loops[0]); }
        }
    }
    function dimension(e) {
        const doc = owners.get(e), style = doc?.production?.dimstyles.find(s => s.name === e.dimstyle), v = { ...style, ...e };
        if (!e.kind || e.kind === 'aligned') { const result = baseDimension(v); if (style && !e.text) result.text.text = style.prefix + (V.dist(e.points[0], e.points[1]) * style.scale).toFixed(style.precision) + style.suffix; return result; }
        const p = e.points, n = V.norm(e.normal || [0, 0, 1]), h = v.textHeight || 2.5, segments = [];
        let position, text, direction = basis(n).x;
        const format = value => (v.prefix || '') + (value * (v.scale || 1)).toFixed(v.precision ?? 2) + (v.suffix || '');
        const arrow = (a, toward) => { const u = V.norm(V.sub(toward, a)), side = V.cross(n, u); segments.push([a, V.add(a, V.add(V.mul(u, h), V.mul(side, h * .25)))], [a, V.add(a, V.add(V.mul(u, h), V.mul(side, -h * .25)))]); };
        if (e.kind === 'linear') {
            const u = V.norm(e.measureAxis || [1, 0, 0]), side = V.norm(V.cross(n, u)), a = V.add(p[0], V.mul(side, e.offset || 0)), b = V.add(a, V.mul(u, V.dot(V.sub(p[1], p[0]), u)));
            segments.push([p[0], a], [p[1], b], [a, b]); arrow(a, b); arrow(b, a); direction = u; position = V.add(V.lerp(a, b, .5), V.mul(side, h * .5)); text = format(Math.abs(V.dot(V.sub(p[1], p[0]), u)));
        } else if (e.kind === 'radius' || e.kind === 'diameter') {
            const c = p[0], q = p[1], other = e.kind === 'diameter' ? V.sub(V.mul(c, 2), q) : c;
            segments.push([other, q]); arrow(q, other); if (e.kind === 'diameter') arrow(other, q);
            direction = V.norm(V.sub(q, c)); position = V.add(q, V.mul(direction, h * 1.4)); text = (e.kind === 'radius' ? 'R' : '⌀') + format(V.dist(c, q) * (e.kind === 'diameter' ? 2 : 1));
        } else if (e.kind === 'angular') {
            if (p.length !== 3) throw Error('Angular dimensions require a vertex and two ray points.');
            const u = V.norm(V.sub(p[1], p[0])), w = V.norm(V.sub(p[2], p[0])), side = V.cross(n, u), a = angle(Math.atan2(V.dot(w, side), V.dot(w, u))), radius = Math.abs(e.offset || Math.min(V.dist(p[0], p[1]), V.dist(p[0], p[2])) * .6);
            const curve = t => V.add(p[0], V.add(V.mul(u, radius * Math.cos(t)), V.mul(side, radius * Math.sin(t))));
            const count = Math.max(8, Math.ceil(a * 20)); for (let i = 0; i < count; i++) segments.push([curve(a * i / count), curve(a * (i + 1) / count)]);
            segments.push([p[0], curve(0)], [p[0], curve(a)]); arrow(curve(0), curve(.1)); arrow(curve(a), curve(a - .1));
            position = V.add(curve(a / 2), V.mul(V.norm(V.sub(curve(a / 2), p[0])), h)); text = (a * 180 / Math.PI).toFixed(v.precision ?? 2) + '°';
        } else {
            const a = p[0], b = p[1], axis = e.kind === 'ordinate-y' ? 1 : 0, end = V.add(b, e.kind === 'ordinate-y' ? [e.offset || h * 5, 0, 0] : [0, e.offset || h * 5, 0]);
            segments.push([b, end]); position = end; text = format(b[axis] - a[axis]);
        }
        return { segments, text: { position: e.textPosition || position, text: e.text || text, height: h, direction, normal: n, rotation: Math.atan2(direction[1], direction[0]), align: 'center' } };
    }
    function hatchGeometry(e) {
        const loops = e.loops || [e.points], b = basis(e.normal || [0, 0, 1]), origin = loops[0][0];
        const q = loops.map(loop => loop.map(p => [V.dot(V.sub(p, origin), b.x), V.dot(V.sub(p, origin), b.y)]));
        const out = empty(), world = p => V.add(origin, V.add(V.mul(b.x, p[0]), V.mul(b.y, p[1])));
        for (const loop of loops) for (let i = 0; i < loop.length; i++) out.segments.push([loop[i], loop[(i + 1) % loop.length]]);
        const edges = q.flatMap(loop => loop.map((a, i) => [a, loop[(i + 1) % loop.length]]));
        const crossings = y => edges.filter(([a, z]) => (a[1] <= y && z[1] > y) || (z[1] <= y && a[1] > y)).map(([a, z]) => ({ a, z, x: a[0] + (y - a[1]) * (z[0] - a[0]) / (z[1] - a[1]) })).sort((a, z) => a.x - z.x);
        const at = (edge, y) => [edge.a[0] + (y - edge.a[1]) * (edge.z[0] - edge.a[0]) / (edge.z[1] - edge.a[1]), y];
        if (e.pattern === 'solid') {
            const ys = [...new Set(q.flat().map(p => p[1]))].sort((a, z) => a - z);
            if (ys.length * edges.length > 2e7) throw Error('Hatch complexity limit exceeded.');
            for (let j = 0; j < ys.length - 1; j++) { const lo = ys[j], hi = ys[j + 1], xs = crossings((lo + hi) / 2);
                for (let i = 0; i + 1 < xs.length; i += 2) { const pts = [at(xs[i], lo), at(xs[i + 1], lo), at(xs[i + 1], hi), at(xs[i], hi)].map(world);
                    for (const ids of [[0, 1, 2], [0, 2, 3]]) if (V.len(V.cross(V.sub(pts[ids[1]], pts[ids[0]]), V.sub(pts[ids[2]], pts[ids[0]]))) > EPS) out.triangles.push({ points: ids.map(i => pts[i]), normal: b.n });
                }
            }
        } else {
            const angles = e.pattern === 'cross' ? [e.angle ?? Math.PI / 4, (e.angle ?? Math.PI / 4) + Math.PI / 2] : [e.angle ?? Math.PI / 4];
            num(e.spacing, 'Hatch spacing', 1e-7, 1e9);
            for (const a of angles) {
                const c = Math.cos(a), s = Math.sin(a), rotated = q.map(loop => loop.map(p => [p[0] * c + p[1] * s, -p[0] * s + p[1] * c]));
                const all = rotated.flat(), ys = all.map(p => p[1]), low = Math.min(...ys), high = Math.max(...ys);
                if ((high - low) / e.spacing > 10000 || (high - low) / e.spacing * all.length > 2e7) throw Error('Hatch line limit exceeded; increase spacing.');
                for (let y = Math.ceil(low / e.spacing) * e.spacing; y < high; y += e.spacing) { const xs = [];
                    for (const loop of rotated) for (let i = 0; i < loop.length; i++) { const p = loop[i], z = loop[(i + 1) % loop.length]; if ((p[1] <= y && z[1] > y) || (z[1] <= y && p[1] > y)) xs.push(p[0] + (y - p[1]) * (z[0] - p[0]) / (z[1] - p[1])); }
                    xs.sort((a, z) => a - z); for (let i = 0; i + 1 < xs.length; i += 2) out.segments.push([xs[i], xs[i + 1]].map(x => world([x * c - y * s, x * s + y * c])));
                }
            }
        }
        out.points = out.segments.flat(); out.snaps = loops.flat().map(p => ({ point: p, type: 'endpoint' })); return out;
    }
    function tableGeometry(e) {
        const out = empty(), axes = G.textAxes(e), rows = e.cells.length, cols = e.cells[0].length, rh = e.rowHeight, cw = e.columnWidth;
        const at = (x, y) => V.add(e.position, V.add(V.mul(axes.x, x), V.mul(axes.y, -y)));
        for (let i = 0; i <= rows; i++) out.segments.push([at(0, i * rh), at(cols * cw, i * rh)]);
        for (let j = 0; j <= cols; j++) out.segments.push([at(j * cw, 0), at(j * cw, rows * rh)]);
        for (let i = 0; i < rows; i++) for (let j = 0; j < cols; j++) out.texts.push({ type: 'TEXT', position: at((j + .08) * cw, (i + .7) * rh), text: e.cells[i][j], height: e.textHeight || rh * .45, normal: axes.n, direction: axes.x, rotation: e.rotation || 0 });
        out.points = out.segments.flat(); out.snaps = [{ point: e.position, type: 'insertion' }]; return out;
    }
    function geometry(e, tolerance = .5) {
        const doc = owners.get(e);
        if (e.type === 'INSERT') { if (!doc) return empty(); const out = empty(); for (const c of expand(doc, e, [], {n:0}, true)) { owners.set(c, doc); merge(out, G.geometry(c, tolerance)); } out.snaps.push({ point: M.point(e.matrix, [0, 0, 0]), type: 'insertion' }); return out; }
        if (e.type === 'TABLE') return tableGeometry(e);
        if (e.type === 'LEADER') { const out = empty(); for (let i = 1; i < e.points.length; i++) out.segments.push([e.points[i - 1], e.points[i]]); const a = e.points[0], u = V.norm(V.sub(e.points[1], a)), side = V.cross(e.normal || [0, 0, 1], u), h = e.textHeight || 2.5; out.segments.push([a, V.add(a, V.add(V.mul(u, h), V.mul(side, h / 4)))], [a, V.add(a, V.add(V.mul(u, h), V.mul(side, -h / 4)))]); out.texts.push({ position: e.points.at(-1), text: e.text, height: h, normal: e.normal }); out.points = out.segments.flat(); return out; }
        if (e.type === 'HATCH' && e.loops) return hatchGeometry(e);
        if (e.type === 'DIMENSION' && (e.kind || e.dimstyle)) { const d = dimension(e), out = empty(); out.segments = d.segments; out.texts = [d.text]; out.points = [...d.segments.flat(), d.text.position]; out.snaps = e.points.map(p => ({ point: p, type: 'endpoint' })); return out; }
        return baseGeometry(e, tolerance);
    }
    function transform(e, matrix) {
        if (e.type === 'INSERT') return { ...clone(e), matrix: Array.from(M.multiply(matrix, e.matrix)) };
        const out = baseTransform(e, matrix);
        if (e.loops) out.loops = e.loops.map(loop => loop.map(p => M.point(matrix, p)));
        if (e.measureAxis) out.measureAxis = V.norm(M.point(matrix, e.measureAxis, 0));
        if (e.textPosition) out.textPosition = M.point(matrix, e.textPosition);
        if (e.type === 'TABLE') {
            const a = G.textAxes(e), x = M.point(matrix, a.x, 0), y = M.point(matrix, a.y, 0); out.direction = V.norm(x); out.normal = V.norm(V.cross(x, y)); out.columnWidth *= V.len(x); out.rowHeight *= V.len(y);
        }
        return out;
    }
    function flattenGeometry(e, doc) {
        if (doc) owners.set(e, doc); const g = G.geometry(e), common = { layer: e.layer, color: e.color, linetype: e.linetype, lineweight: e.lineweight };
        return [...g.segments.map(points => ({ ...common, type: 'LINE', points })), ...g.texts.map(t => ({ ...common, ...t, type: 'TEXT' })), ...g.triangles.map(t => ({ ...common, type: 'MESH', vertices: t.points, faces: [[0, 1, 2]] }))];
    }
    function stretch(doc, ids, min, max, delta) {
        point(min); point(max); point(delta); const inside = p => p[0] >= min[0] && p[0] <= max[0] && p[1] >= min[1] && p[1] <= max[1];
        doc.transaction('Stretch crossing vertices', () => { for (const id of ids) { const e = doc.byId.get(id); if (!e || !doc.editable(e)) throw Error('Select editable entities.');
            if (e.type === 'LINE' || e.type === 'POLYLINE') { const out = clone(e); if (e.bulges?.some(Boolean)) throw Error('Stretch bulged polylines after exploding their arcs.'); out.points = e.points.map(p => inside(p) ? V.add(p, delta) : p); doc.replace(id, out); }
            else { const g = doc.geometry(e); if (g.points.length && g.points.every(inside)) doc.transform([id], M.translation(...delta)); else if (g.points.some(inside)) throw Error('Partial stretch supports straight lines/polylines only.'); }
        } });
    }
    function breakEntity(doc, id, first, second) {
        const e = doc.byId.get(id); if (!e || !doc.editable(e)) throw Error('Select an editable curve.'); point(first); point(second); const parts = [];
        if (e.type === 'LINE') { const d = V.sub(e.points[1], e.points[0]), ll = V.dot(d, d); if (ll < EPS) throw Error('Degenerate line.'); const t = [first, second].map(p => V.dot(V.sub(p, e.points[0]), d) / ll).sort((a, b) => a - b); if (t[0] < -EPS || t[1] > 1 + EPS) throw Error('Break points must lie within the line.'); if (t[0] > EPS) parts.push({ ...e, points: [e.points[0], V.lerp(...e.points, t[0])] }); if (t[1] < 1 - EPS) parts.push({ ...e, points: [V.lerp(...e.points, t[1]), e.points[1]] });
        } else if (e.type === 'CIRCLE' || e.type === 'ARC') { const axes = G.conicAxes(e), parameter = p => angle(Math.atan2(V.dot(V.sub(p, e.center), V.norm(axes.y)), V.dot(V.sub(p, e.center), V.norm(axes.x)))), a = parameter(first), b = parameter(second);
            if (Math.abs(angle(b - a)) < EPS) throw Error('Circular break needs two distinct points.');
            if (e.type === 'CIRCLE') parts.push({ ...e, type: 'ARC', startAngle: b, endAngle: a });
            else { const start = e.startAngle || 0, total = sweep(start, e.endAngle), ta = angle(a - start), tb = angle(b - start); if (ta > total + EPS || tb > total + EPS || ta > tb) throw Error('Break points must follow the arc direction.'); if (ta > EPS) parts.push({ ...e, endAngle: a }); if (tb < total - EPS) parts.push({ ...e, startAngle: b }); }
        } else throw Error('BREAK supports LINE, CIRCLE and ARC.');
        const ids = []; doc.transaction('Break curve', () => { doc.remove([id]); for (const p of parts) { delete p.id; ids.push(doc.add(p).id); } doc.selection = new Set(ids); }); return ids;
    }
    function align(doc, ids, source, target, scale = false) {
        source.forEach(point); target.forEach(point); if (source.length !== 2 || target.length !== 2) throw Error('ALIGN needs two source and two target points.');
        const a = V.sub(source[1], source[0]), b = V.sub(target[1], target[0]); if (V.len(a) < EPS || V.len(b) < EPS) throw Error('ALIGN points must be distinct.');
        const u = V.norm(a), v = V.norm(b); let axis = V.cross(u, v), theta = Math.acos(Math.max(-1, Math.min(1, V.dot(u, v))));
        if (V.len(axis) < EPS) axis = basis(u).x;
        const matrix = M.multiply(M.translation(...target[0]), M.multiply(M.rotation(theta, axis), M.multiply(M.scale(scale ? V.len(b) / V.len(a) : 1), M.translation(...V.mul(source[0], -1)))));
        doc.transaction('Align objects', () => doc.transform(ids, matrix));
    }
    function validateLayout(l) {
        name(l.name); num(l.width, 'Paper width', 10, 3000); num(l.height, 'Paper height', 10, 3000);
        if (!Array.isArray(l.viewports) || l.viewports.length > 32) throw Error('Invalid viewports.');
        for (const v of l.viewports) { ['x','y','width','height','scale'].forEach(k => num(v[k], 'Viewport ' + k, k === 'x' || k === 'y' ? 0 : 1e-7, 1e9)); point(v.center); if (v.x + v.width > l.width || v.y + v.height > l.height) throw Error('Viewport extends beyond paper.'); }
    }
    const xml = s => String(s).replace(/[&<>"']/g, c => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&apos;' }[c]));
    function layoutSVG(doc, layout) {
        validateLayout(layout); const out = [`<svg xmlns="http://www.w3.org/2000/svg" width="${layout.width}mm" height="${layout.height}mm" viewBox="0 0 ${layout.width} ${layout.height}"><title>${xml(layout.name)}</title><rect width="100%" height="100%" fill="white"/>`];
        for (const [i, v] of layout.viewports.entries()) {
            const factor = MM[doc.units] / v.scale, at = p => [v.x + v.width / 2 + (p[0] - v.center[0]) * factor, v.y + v.height / 2 - (p[1] - v.center[1]) * factor];
            out.push(`<defs><clipPath id="vp${i}"><rect x="${v.x}" y="${v.y}" width="${v.width}" height="${v.height}"/></clipPath></defs><g clip-path="url(#vp${i})" fill="none" stroke="#111" stroke-linecap="round" stroke-linejoin="round">`);
            for (const e of doc.entities) if (doc.visible(e) && !v.frozenLayers?.includes(e.layer)) { const g = doc.geometry(e), width = e.lineweight || doc.layer(e).lineweight || .25;
                if (g.segments.length) out.push(`<path stroke-width="${width}" d="${g.segments.map(s => 'M'+at(s[0]).join(',')+'L'+at(s[1]).join(',')).join('')}"/>`);
                for (const t of g.texts) { if(t.composition&&K.MText){out.push(K.MText.svg(t,at,"#111",xml,true));continue;}if(K.Fonts && t.fontFamily){out.push(K.Fonts.svgText(t,at,"#111",xml));continue;}const [x,y] = at(t.position); out.push(`<text x="${x}" y="${y}" stroke="none" fill="#111" font-family="Arial,sans-serif" font-size="${t.height * factor}" text-anchor="${t.align === 'center' ? 'middle' : t.align === 'right' ? 'end' : 'start'}" transform="rotate(${-(t.rotation || 0) * 180 / Math.PI} ${x} ${y})">${xml(t.text)}</text>`); }
                if (e.type === 'HATCH') for (const t of g.triangles) out.push(`<polygon points="${t.points.map(p=>at(p).join(',')).join(' ')}" fill="#333" stroke="none"/>`);
            } out.push('</g>'); if (v.border !== false) out.push(`<rect x="${v.x}" y="${v.y}" width="${v.width}" height="${v.height}" fill="none" stroke="#555" stroke-width="0.18"/>`);
        }
        out.push(`<text x="10" y="${layout.height - 6}" font-family="Arial,sans-serif" font-size="3">${xml(doc.name + ' — ' + layout.name)}</text></svg>`); return out.join('\n');
    }
    function addLayout(doc, label = 'Layout 1', scale = 1) {
        const layout = { name: label, width: 420, height: 297, viewports: [{ x: 10, y: 10, width: 400, height: 265, center: [0,0,0], scale, locked: true, border: true, frozenLayers: [] }] }; validateLayout(layout);
        doc.transaction('Create layout', () => ensure(doc).layouts.push(layout)); return layout;
    }
    K.Production = { defaults, ensure, bind, validate, validateEntity, CUSTOM, MM, num, point, name, frame, toWorld, toLocal, expand, renderEntities, defineBlock, insertBlock, explode, attach, reference, associations, dimension, hatchGeometry, flattenGeometry, stretch, breakEntity, align, validateLayout, layoutSVG, addLayout, owners };
    G.geometry = geometry; G.transform = transform; G.dimension = dimension;
})(typeof window !== 'undefined' ? window : globalThis);
