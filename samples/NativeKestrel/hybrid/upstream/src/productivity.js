/* Kestrel CAD: transactional drafting productivity. Original MIT implementation. */
(function (root) {
    'use strict';
    const K = root.Kestrel, G = K.Geo, P = K.Production, {V, M, TAU} = K.Math;
    const MAX_STATIONS = 10000, EPS = 1e-9;
    const clone = K.clone;
    const finite = (value, label, minimum = -1e12, maximum = 1e12) => {
        if (typeof value !== 'number' || !Number.isFinite(value) || value < minimum || value > maximum)
            throw Error('Invalid ' + label + '.');
        return value;
    };
    function circular(e) {
        const axes = G.conicAxes(e), radius = V.len(axes.x), other = V.len(axes.y);
        if (radius < EPS || Math.abs(radius - other) > 1e-8 * radius || Math.abs(V.dot(axes.x, axes.y)) > 1e-8 * radius * other)
            throw Error('This operation requires a circular curve, not a distorted ellipse.');
        return {axes, radius, normal: V.norm(V.cross(axes.x, axes.y))};
    }
    function linearSegment(a, b) {
        const delta = V.sub(b, a), length = V.len(delta);
        if (length <= EPS) throw Error('The curve has a zero-length segment.');
        return {length, point: t => V.lerp(a, b, t), tangent: () => V.mul(delta, 1 / length)};
    }
    function bulgeSegment(a, b, bulge, normal) {
        finite(bulge, 'polyline bulge', -1e8, 1e8);
        if (Math.abs(bulge) < 1e-12) return linearSegment(a, b);
        const chord = V.sub(b, a), distance = V.len(chord), n = V.norm(normal);
        if (distance <= EPS || V.len(n) < .5 || Math.abs(V.dot(chord, n)) > 1e-8 * distance)
            throw Error('A bulged segment must be nondegenerate and lie in its normal plane.');
        const center = V.add(V.lerp(a, b, .5), V.mul(V.norm(V.cross(n, chord)), distance * (1 - bulge * bulge) / (4 * bulge)));
        const radial = V.sub(a, center), radius = V.len(radial), sweep = 4 * Math.atan(bulge);
        const rotated = t => M.point(M.rotation(sweep * t, n), radial, 0);
        return {length: Math.abs(sweep) * radius, point: t => V.add(center, rotated(t)), tangent: t => V.mul(V.norm(V.cross(n, rotated(t))), Math.sign(sweep))};
    }
    function curve(e) {
        if (!e || e.solid) throw Error('Select a drafting curve, not a native body.');
        let segments = [], closed = false, normal = e.normal || [0, 0, 1];
        if (e.type === 'LINE' || e.type === 'POLYLINE') {
            const points = e.points;
            if (!Array.isArray(points) || points.length < 2 || e.type === 'LINE' && points.length !== 2)
                throw Error('Invalid line/polyline geometry.');
            closed = e.type === 'POLYLINE' && !!e.closed;
            for (let i = 0; i < points.length - (closed ? 0 : 1); i++)
                segments.push(bulgeSegment(points[i], points[(i + 1) % points.length], e.bulges?.[i] || 0, normal));
        } else if (e.type === 'CIRCLE' || e.type === 'ARC') {
            const c = circular(e), start = e.startAngle || 0, sweep = e.type === 'CIRCLE' ? TAU : K.Math.sweep(start, e.endAngle);
            normal = c.normal; closed = e.type === 'CIRCLE';
            segments = [{length: c.radius * sweep,
                point: t => G.conicPoint(e, start + sweep * t),
                tangent: t => V.norm(V.add(V.mul(c.axes.x, -Math.sin(start + sweep * t)), V.mul(c.axes.y, Math.cos(start + sweep * t))))}];
        } else throw Error('Analytic stations support LINE, POLYLINE, CIRCLE and ARC.');
        const length = segments.reduce((n, s) => n + s.length, 0);
        finite(length, 'curve length', EPS, 1e12);
        function at(distance) {
            finite(distance, 'station distance', -EPS, length + EPS * Math.max(1, length));
            let remaining = Math.max(0, Math.min(length, distance));
            for (let i = 0; i < segments.length; i++) {
                const s = segments[i];
                if (remaining <= s.length || i === segments.length - 1) {
                    const t = Math.max(0, Math.min(1, remaining / s.length));
                    return {distance, point: s.point(t), tangent: s.tangent(t), normal};
                }
                remaining -= s.length;
            }
            throw Error('Invalid curve station.');
        }
        return {length, closed, normal, at};
    }
    function stations(e, mode, value) {
        const c = curve(e), distances = [];
        if (mode === 'divide') {
            finite(value, 'division count', 2, MAX_STATIONS);
            if (!Number.isInteger(value)) throw Error('Division count must be an integer.');
            for (let i = c.closed ? 0 : 1; i < value; i++) distances.push(c.length * i / value);
        } else if (mode === 'measure') {
            finite(value, 'station spacing', EPS, 1e12);
            const count = Math.floor((c.length + EPS * Math.max(1, c.length)) / value);
            if (count > MAX_STATIONS) throw Error('Station count exceeds 10,000; increase the spacing.');
            for (let i = 1; i <= count; i++) {
                const distance = Math.min(c.length, i * value);
                if (c.closed && distance >= c.length - EPS * Math.max(1, c.length)) break;
                distances.push(distance);
            }
        } else throw Error('Choose divide or measure.');
        return distances.map(c.at);
    }
    function editable(doc, ids) {
        if (!Array.isArray(ids) || !ids.length || new Set(ids).size !== ids.length) throw Error('Select distinct editable objects.');
        const entities = ids.map(id => doc.byId.get(id));
        if (entities.some(e => !e || !doc.editable(e))) throw Error('A selected object is missing, hidden or locked.');
        return entities;
    }
    function guardConstraints(doc, ids) {
        const s = K.Constraints?.state(doc), set = new Set(ids);
        if (s?.constraints.some(c => !c.suppressed && ['a', 'b', 'c'].some(k => set.has(c[k]?.entity))))
            throw Error('Edit the driving dimensions or remove these curve constraints first.');
    }
    function mark(doc, id, mode, value, options = {}) {
        const e = editable(doc, [id])[0], points = stations(e, mode, value), block = options.block || null;
        const scale = finite(options.scale ?? 1, 'marker scale', 1e-8, 1e8);
        if (block && !P.ensure(doc).blocks.some(b => b.id === block)) throw Error('Unknown marker block.');
        const added = [];
        if (!points.length) return added;
        doc.transaction(mode === 'divide' ? 'Divide curve' : 'Measure curve stations', () => {
            for (const s of points) {
                let marker;
                if (block) {
                    const x = options.align === false ? [1, 0, 0] : s.tangent;
                    let y = V.norm(V.cross(s.normal, x));
                    if (V.len(y) < .5) y = K.Math.basis(x).x;
                    const z = V.norm(V.cross(x, y)), matrix = M.identity();
                    matrix.set(V.mul(x, scale), 0); matrix.set(V.mul(y, scale), 4); matrix.set(V.mul(z, scale), 8); matrix.set(s.point, 12);
                    marker = doc.add('INSERT', {block, matrix: Array.from(matrix), attributes: {}, layer: e.layer});
                } else marker = doc.add('POINT', {position: s.point, layer: e.layer, color: e.color, size: options.size || 1});
                added.push(marker.id);
            }
            doc.selection = new Set(added);
        });
        return added;
    }
    function lengthen(doc, id, mode, value, end = 'end') {
        const e = editable(doc, [id])[0]; guardConstraints(doc, [id]);
        if (!['LINE', 'ARC'].includes(e.type)) throw Error('Lengthen supports LINE and circular ARC.');
        if (!['start', 'end'].includes(end)) throw Error('Choose the start or end of the curve.');
        finite(value, 'length value'); const c = curve(e);
        const target = mode === 'total' ? value : mode === 'delta' ? c.length + value : mode === 'percent' ? c.length * value / 100 : NaN;
        finite(target, 'resulting length', EPS, 1e12); const out = clone(e);
        if (e.type === 'LINE') {
            const moving = end === 'start' ? 0 : 1, fixed = 1 - moving;
            out.points[moving] = V.add(e.points[fixed], V.mul(V.norm(V.sub(e.points[moving], e.points[fixed])), target));
        } else {
            const sweep = target / circular(e).radius;
            if (sweep >= TAU - 1e-10) throw Error('An arc must remain shorter than a full circle.');
            if (end === 'start') out.startAngle = e.endAngle - sweep;
            else out.endAngle = (e.startAngle || 0) + sweep;
        }
        doc.transaction('Lengthen curve', () => doc.replace(id, out));
        return target;
    }
    function reversed(e) {
        if (e.solid) throw Error('Native bodies are not reversible drafting curves.');
        const out = clone(e);
        if (e.type === 'LINE' || e.type === 'POLYLINE') {
            const n = e.points.length; out.points.reverse();
            if (e.bulges) out.bulges = Array.from({length: n}, (_, i) => !e.closed && i === n - 1 ? 0 : -(e.bulges[(n - 2 - i + n) % n] || 0));
        } else if (e.type === 'SPLINE') {
            if (out.controlPoints) out.controlPoints.reverse(); else out.points.reverse();
            if (out.weights) out.weights.reverse();
            if (out.knots?.length) {const sum = out.knots[0] + out.knots.at(-1); out.knots = out.knots.reverse().map(k => sum - k);}
        } else if (['ARC', 'CIRCLE', 'ELLIPSE'].includes(e.type)) {
            const axes = G.conicAxes(e); out.axisX = axes.x.slice(); out.axisY = V.mul(axes.y, -1); out.normal = V.norm(V.cross(out.axisX, out.axisY));
            if (e.type === 'ARC' || e.endAngle != null) {out.startAngle = -(e.endAngle ?? TAU); out.endAngle = -(e.startAngle || 0);}
        } else throw Error('Reverse supports lines, polylines, splines and conic curves.');
        return out;
    }
    function reverse(doc, ids) {
        const entities = editable(doc, ids); guardConstraints(doc, ids);
        const results = entities.map(reversed);
        doc.transaction('Reverse curve direction', () => results.forEach(e => doc.replace(e.id, e)));
    }
    function matches(doc, e, query = {}) {
        if (query.type && query.type !== '*' && e.type !== query.type) return false;
        if (query.layer && query.layer !== '*' && e.layer !== query.layer && doc.layer(e).name !== query.layer) return false;
        if (query.block && e.block !== query.block) return false;
        if (query.closed != null && G.closed(e) !== query.closed) return false;
        if (query.color) {
            const color = !e.color || e.color === 'bylayer' ? doc.layer(e).color : e.color;
            if (String(color).toLowerCase() !== String(query.color).toLowerCase()) return false;
        }
        if (query.text) {
            const value = [e.text || '', ...(e.cells || []).flat(), ...Object.values(e.attributes || {})].join('\n');
            if (!(query.caseSensitive ? value.includes(query.text) : value.toLowerCase().includes(String(query.text).toLowerCase()))) return false;
        }
        return true;
    }
    function select(doc, query = {}, mode = 'replace') {
        if (!['replace', 'add', 'remove', 'intersect'].includes(mode)) throw Error('Unknown selection mode.');
        const ids = doc.entities.filter(e => doc.editable(e) && matches(doc, e, query)).map(e => e.id), found = new Set(ids), old = doc.selection;
        doc.selection = mode === 'replace' ? found : mode === 'add' ? new Set([...old, ...ids]) : new Set([...old].filter(id => mode === 'remove' ? !found.has(id) : found.has(id)));
        return [...doc.selection];
    }
    function area(e) {
        if (e.type === 'CIRCLE') return Math.PI * circular(e).radius ** 2;
        if (e.type !== 'POLYLINE' || !e.closed || e.points.length < 3) return null;
        const n = V.norm(e.normal || K.Math.faceNormal(e.points)), axes = K.Math.basis(n), origin = e.points[0];
        if (V.len(n) < .5 || e.points.some(p => Math.abs(V.dot(V.sub(p, origin), n)) > 1e-7)) return null;
        const flat = e.points.map(p => [V.dot(V.sub(p, origin), axes.x), V.dot(V.sub(p, origin), axes.y)]);
        let result = K.Math.polygonArea(flat);
        for (let i = 0; i < e.points.length; i++) {
            const b = e.bulges?.[i] || 0;
            if (Math.abs(b) > 1e-12) {
                const chord = V.dist(e.points[i], e.points[(i + 1) % e.points.length]), r = chord * (1 + b * b) / (4 * Math.abs(b)), theta = 4 * Math.atan(b);
                result += r * r * (theta - Math.sin(theta)) / 2;
            }
        }
        return Math.abs(result);
    }
    function rows(doc, ids = null) {
        const entities = ids ? ids.map(id => doc.byId.get(id)).filter(Boolean) : doc.entities.filter(e => doc.visible(e));
        const blocks = new Map(P.ensure(doc).blocks.map(b => [b.id, b.name]));
        return entities.map(e => {
            let length = null, enclosed = null;
            try {length = curve(e).length;} catch (_) {}
            try {enclosed = area(e);} catch (_) {}
            return {id: e.id, type: e.type, layer: doc.layer(e).name, block: blocks.get(e.block) || '', length, area: enclosed, radius: e.radius ?? null,
                text: e.text || '', attributes: clone(e.attributes || {}), units: doc.units};
        });
    }
    function csv(records) {
        const tags = [...new Set(records.flatMap(r => Object.keys(r.attributes || {})))].sort();
        const columns = ['id', 'type', 'layer', 'block', 'length', 'area', 'radius', 'text', 'units'];
        const cell = value => {
            if (value == null) return '';
            if (typeof value === 'number') return Number.isFinite(value) ? String(value) : '';
            let s = String(value); if (/^[\s]*[=+\-@]/.test(s) || /^[\t\r\n]/.test(s)) s = "'" + s;
            return '"' + s.replace(/"/g, '""') + '"';
        };
        return [columns.concat(tags.map(t => 'attribute:' + t)).map(cell).join(','), ...records.map(r => columns.map(c => r[c]).concat(tags.map(t => r.attributes?.[t] || '')).map(cell).join(','))].join('\r\n') + '\r\n';
    }
    function count(doc, ids = null) {
        const groups = new Map();
        for (const row of rows(doc, ids)) {
            const key = JSON.stringify([row.type, row.layer, row.block]);
            if (!groups.has(key)) groups.set(key, {type: row.type, layer: row.layer, block: row.block, count: 0, totalLength: 0, totalArea: 0});
            const item = groups.get(key); item.count++; item.totalLength += row.length || 0; item.totalArea += row.area || 0;
        }
        return [...groups.values()];
    }
    function extractionTable(doc, ids = null, position = [0, 0, 0]) {
        P.point(position); const records = rows(doc, ids);
        if (!records.length || records.length > 999) throw Error('Extraction tables require 1–999 source objects. Export CSV for larger selections.');
        const number = n => n == null ? '' : Number(n.toPrecision(10)).toString();
        const cells = [['Type', 'Layer', 'Block', 'Length (' + doc.units + ')', 'Area (' + doc.units + '²)'], ...records.map(r => [r.type, r.layer, r.block, number(r.length), number(r.area)])];
        let table; doc.transaction('Extract drawing data to table', () => {
            table = doc.add('TABLE', {position, cells, rowHeight: 8, columnWidth: 45, textHeight: 2.5}); doc.selection = new Set([table.id]);
        }); return table;
    }
    const STATE_KEYS = ['visible', 'locked', 'color', 'linetype', 'lineweight'];
    function layerStates(doc) {const p = P.ensure(doc); if (!p.layerStates) p.layerStates = []; return p.layerStates;}
    function saveLayers(doc, name, replace = false) {
        P.name(name); const states = layerStates(doc), prior = states.find(s => s.name.toLowerCase() === name.toLowerCase());
        if (prior && !replace) throw Error('Layer state already exists. Choose overwrite explicitly.');
        if (!prior && states.length >= 128) throw Error('Layer-state limit is 128.');
        const state = {name, currentLayer: doc.currentLayer, layers: doc.layers.map(l => ({id: l.id, ...Object.fromEntries(STATE_KEYS.map(k => [k, l[k]]))}))};
        doc.transaction('Save layer state', () => {const list = layerStates(doc); if (prior) list[list.indexOf(prior)] = state; else list.push(state);});
        return state;
    }
    function restoreLayers(doc, name) {
        const state = layerStates(doc).find(s => s.name === name); if (!state || !Array.isArray(state.layers)) throw Error('Unknown or invalid layer state.');
        doc.transaction('Restore layer state', () => {
            for (const saved of state.layers) {const layer = doc.layers.find(l => l.id === saved.id); if (!layer) continue; for (const key of STATE_KEYS) if (saved[key] !== undefined) layer[key] = clone(saved[key]);}
            if (doc.layers.some(l => l.id === state.currentLayer)) doc.currentLayer = state.currentLayer;
            doc.selection = new Set([...doc.selection].filter(id => {const e = doc.byId.get(id); return e && doc.editable(e);}));
        });
    }
    function deleteLayers(doc, name) {
        if (!layerStates(doc).some(s => s.name === name)) throw Error('Unknown layer state.');
        doc.transaction('Delete layer state', () => {P.ensure(doc).layerStates = layerStates(doc).filter(s => s.name !== name);});
    }
    // Persisted states are untrusted project data, not just values from the dialog.
    const validateProduction = P.validate;
    P.validate = function(data) {
        validateProduction(data);
        const states = data.production.layerStates;
        if (states == null) return;
        if (!Array.isArray(states) || states.length > 128) throw Error('Invalid layer-state table.');
        const names = new Set();
        for (const state of states) {
            if (!state || typeof state !== 'object') throw Error('Invalid layer state.');
            P.name(state.name);
            if (names.has(state.name.toLowerCase())) throw Error('Duplicate layer-state name.');
            names.add(state.name.toLowerCase());
            const id = value => typeof value === 'string' && /^[-a-zA-Z0-9_.:]{1,128}$/.test(value);
            if (!id(state.currentLayer) || !Array.isArray(state.layers) || state.layers.length > 2048) throw Error('Invalid saved layer list.');
            const ids = new Set();
            for (const layer of state.layers) {
                if (!layer || !id(layer.id) || ids.has(layer.id)) throw Error('Invalid saved layer identity.');
                ids.add(layer.id);
                if (typeof layer.visible !== 'boolean' || typeof layer.locked !== 'boolean' || !/^#[0-9a-f]{6}$/i.test(layer.color)) throw Error('Invalid saved layer appearance.');
                finite(layer.lineweight, 'saved lineweight', .01, 5);
                if (typeof layer.linetype !== 'string' || layer.linetype.length > 255 || /[\x00-\x1f]/.test(layer.linetype)) throw Error('Invalid saved linetype.');
            }
        }
    };
    K.Productivity = {version: 1, curve, stations, mark, lengthen, reversed, reverse, matches, select, area, rows, csv, count, extractionTable, layerStates, saveLayers, restoreLayers, deleteLayers};
})(typeof window !== 'undefined' ? window : globalThis);
